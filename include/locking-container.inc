#include "locks.hpp"
#include "lock-auth.hpp"
#include "object-proxy.hpp"
#include "multi-lock.hpp"

namespace lc {

//locks.hpp

  lock_base::order_type lock_base::get_order() const {
    return 0;
  }


  rw_lock::rw_lock() : readers(0), readers_waiting(0), writer(false),
    writer_waiting(false), the_writer(NULL) {
    pthread_mutex_init(&master_lock, NULL);
    pthread_cond_init(&read_wait, NULL);
    pthread_cond_init(&write_wait, NULL);
  }

  rw_lock::count_type rw_lock::lock(lock_auth_base *auth, bool read, bool block, bool test) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    bool writer_reads = auth && the_writer == auth && read;
    bool lock_out     = writer_waiting || readers_waiting;
    //NOTE: see "wait" loops below for these conditions
    bool must_block = writer_waiting || (read? writer : (readers || writer));
    lock_data l(this, block, read, !writer_reads && lock_out,
      !writer_reads && must_block, this->get_order());
    //make sure this is an authorized lock type for the caller
    if (!register_or_test_auth(auth, l, test)) {
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    block = l.block; //(auth. can override blocking mode to allow lock attempt)
    //exception to blocking: if 'auth' holds the write lock and a read is requested
    if (!writer_reads && !block && must_block) {
      if (!test) release_auth(auth, l);
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    if (read) {
      //get a read lock
      ++readers_waiting;
      assert(readers_waiting > 0);
      //NOTE: 'auth' is expected to prevent a deadlock if the caller already has
      //a read lock and there is a writer waiting
      if (!writer_reads) while (writer || writer_waiting) {
        if (pthread_cond_wait(&read_wait, &master_lock) != 0) {
          if (!test) release_auth(auth, l);
          --readers_waiting;
          pthread_mutex_unlock(&master_lock);
          return -1;
        }
      }
      --readers_waiting;
      count_type new_readers = ++readers;
      //if for some strange reason there's an overflow...
      assert((writer_reads || (!writer && !writer_waiting)) && readers > 0);
      pthread_mutex_unlock(&master_lock);
      return new_readers;
    } else {
      //if the caller isn't the first in line for writing, wait until it is
      ++readers_waiting;
      assert(readers_waiting > 0);
      while (writer_waiting) {
        //NOTE: use 'read_wait' here, since that's what a write unlock broadcasts on
        //NOTE: another thread should be blocking in 'write_wait' below
        if (pthread_cond_wait(&read_wait, &master_lock) != 0) {
          if (!test) release_auth(auth, l);
          --readers_waiting;
          pthread_mutex_unlock(&master_lock);
          return -1;
        }
      }
      --readers_waiting;
      writer_waiting = true;
      //get a write lock
      while (writer || readers) {
        if (pthread_cond_wait(&write_wait, &master_lock) != 0) {
          if (!test) release_auth(auth, l);
          writer_waiting = false;
          pthread_mutex_unlock(&master_lock);
          return -1;
        }
      }
      writer_waiting = false;
      writer = true;
      the_writer = auth;
      pthread_mutex_unlock(&master_lock);
      return 0;
    }
  }

  rw_lock::count_type rw_lock::unlock(lock_auth_base *auth, bool read, bool test) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    if (!test) {
      unlock_data l(this, read, this->get_order());
      release_auth(auth, l);
    }
    if (read) {
      assert(((auth && the_writer == auth) || !writer) && readers > 0);
      count_type new_readers = --readers;
      if (!new_readers && writer_waiting) {
        pthread_cond_broadcast(&write_wait);
      }
      pthread_mutex_unlock(&master_lock);
      return new_readers;
    } else {
      assert(writer && ((auth && the_writer == auth) || !readers));
      assert(the_writer == auth);
      writer = false;
      the_writer = NULL;
      if (writer_waiting) {
        pthread_cond_broadcast(&write_wait);
      }
      if (readers_waiting) {
        pthread_cond_broadcast(&read_wait);
      }
      pthread_mutex_unlock(&master_lock);
      return 0;
    }
  }

  rw_lock::~rw_lock() {
    assert(!readers && !readers_waiting && !writer && !writer_waiting);
    pthread_mutex_destroy(&master_lock);
    pthread_cond_destroy(&read_wait);
    pthread_cond_destroy(&write_wait);
  }


  r_lock::r_lock() : readers(0) {}

  r_lock::count_type r_lock::lock(lock_auth_base *auth, bool read, bool /*block*/,
    bool /*test*/) {
    if (!read) return -1;
    //NOTE: because this container can't be a part of a deadlock, it's never
    //considered in use and the lock isn't counted. the 'auth' check is entirely
    //to allow for an auth. that denies all locks.
    lock_data l(this, false, true, false, false, this->get_order());
    if (!register_or_test_auth(auth, l, true)) return -1;
    //NOTE: this is atomic
    count_type new_readers = ++readers;
    //(check the copy!)
    assert(new_readers > 0);
    return new_readers;
  }

  r_lock::count_type r_lock::unlock(lock_auth_base* /*auth*/, bool read, bool /*test*/) {
    if (!read) return -1;
    //NOTE: this is atomic
    count_type new_readers = --readers;
    //(check the copy!)
    assert(new_readers >= 0);
    return new_readers;
  }

  r_lock::~r_lock() {
    assert(!readers);
  }


  w_lock::w_lock() : writer(false), writers_waiting(0) {
    pthread_mutex_init(&master_lock, NULL);
    pthread_cond_init(&write_wait, NULL);
  }

  w_lock::count_type w_lock::lock(lock_auth_base *auth, bool /*read*/, bool block, bool test) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    //NOTE: 'false' is passed instead of 'read' because this can lock out other readers
    lock_data l(this, block, false, writers_waiting, writer, this->get_order());
    if (!register_or_test_auth(auth, l, test)) {
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    block = l.block; //(auth. can override blocking mode to allow lock attempt)
    if (!block && writer) {
      if (!test) release_auth(auth, l);
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    ++writers_waiting;
    assert(writers_waiting > 0);
    while (writer) {
      if (pthread_cond_wait(&write_wait, &master_lock) != 0) {
        if (!test) release_auth(auth, l);
        --writers_waiting;
        pthread_mutex_unlock(&master_lock);
        return -1;
      }
    }
    --writers_waiting;
    writer = true;
    pthread_mutex_unlock(&master_lock);
    return 0;
  }

  w_lock::count_type w_lock::unlock(lock_auth_base *auth, bool /*read*/, bool test) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    if (!test) {
      unlock_data l(this, false, this->get_order());
      release_auth(auth, l);
    }
    assert(writer);
    writer = false;
    if (writers_waiting) {
      pthread_cond_broadcast(&write_wait);
    }
    pthread_mutex_unlock(&master_lock);
    return 0;
  }

  w_lock::~w_lock() {
    assert(!writer && !writers_waiting);
    pthread_mutex_destroy(&master_lock);
    pthread_cond_destroy(&write_wait);
  }


  dumb_lock::dumb_lock() {
    pthread_mutex_init(&master_lock, NULL);
  }

  dumb_lock::count_type dumb_lock::lock(lock_auth_base *auth, bool /*read*/, bool block, bool test) {
    lock_data l(this, block, false, true, true, this->get_order());
    if (!register_or_test_auth(auth, l, test)) return -1;
    block = l.block; //(auth. can override blocking mode to allow lock attempt)
    if ((block? pthread_mutex_lock : pthread_mutex_trylock)(&master_lock) != 0) {
      if (!test) release_auth(auth, l);
      return -1;
    }
    return 0;
  }

  dumb_lock::count_type dumb_lock::unlock(lock_auth_base *auth, bool /*read*/, bool test) {
    if (!test) {
      unlock_data l(this, false, this->get_order());
      release_auth(auth, l);
    }
    return (pthread_mutex_unlock(&master_lock) == 0)? 0 : -1;
  }

  dumb_lock::~dumb_lock() {
    //NOTE: this is the only reasonable way to see if there is currently a lock
    assert(pthread_mutex_trylock(&master_lock) == 0);
    pthread_mutex_destroy(&master_lock);
  }


  broken_lock::count_type broken_lock::lock(lock_auth_base* /*auth*/, bool /*read*/,
    bool /*block*/, bool /*test*/) {
    return -1;
  }

  broken_lock::count_type broken_lock::unlock(lock_auth_base* /*auth*/, bool /*read*/,
    bool /*test*/) {
    return -1;
  }


//lock-auth.hpp

  lock_auth_base::count_type lock_auth_base::reading_count() const { return 0; }
  lock_auth_base::count_type lock_auth_base::writing_count() const { return 0; }

  bool lock_auth_base::order_allowed(order_type order) const {
    //by default, disallow using auth. objects with ordered locks
    return !order;
  }


  lock_auth_rw_lock::lock_auth_rw_lock() : reading(0), writing(0) {}

  lock_auth_rw_lock::count_type lock_auth_rw_lock::reading_count() const { return reading; }
  lock_auth_rw_lock::count_type lock_auth_rw_lock::writing_count() const { return writing; }

  lock_auth_rw_lock::~lock_auth_rw_lock() {
    //NOTE: this can't be in '~lock_auth_base'!
    assert(!this->reading_count() && !this->writing_count());
  }

  bool lock_auth_rw_lock::register_auth(lock_data &l) {
    if (!this->test_auth(l)) return false;
    if (l.read) {
      ++reading;
      assert(reading > 0);
    } else {
      ++writing;
      assert(writing > 0);
    }
    return true;
  }

  bool lock_auth_rw_lock::test_auth(lock_data &l) const {
    if (!this->order_allowed(l.order)) return false;
    //NOTE: 'dumb_lock' just guesses about 'must_block' and 'lock_out', so a
    //non-blocking lock could work. for the other lock types, setting 'block'
    //to false is the same as rejecting the lock
    if ((reading || writing) && l.lock_out) l.block = false;
    if (writing && l.must_block)            l.block = false;
    if (reading && !l.read && l.must_block) l.block = false;
    return true;
  }

  void lock_auth_rw_lock::release_auth(unlock_data &l) {
    if (l.read) {
      //NOTE: don't check 'writing' because there are a few exceptions!
      assert(reading > 0);
      --reading;
    } else {
      //NOTE: don't check 'reading' because there are a few exceptions!
      assert(writing > 0);
      --writing;
    }
  }


  lock_auth_r_lock::lock_auth_r_lock() : reading(0) {}

  lock_auth_r_lock::count_type lock_auth_r_lock::reading_count() const { return reading; }

  lock_auth_r_lock::~lock_auth_r_lock() {
    //NOTE: this can't be in '~lock_auth_base'!
    //NOTE: no point checking 'writing_count', since it's overrides will be ignored here
    assert(!this->reading_count());
  }

  bool lock_auth_r_lock::register_auth(lock_data &l) {
    if (!this->test_auth(l)) return false;
    ++reading;
    assert(reading > 0);
    return true;
  }

  bool lock_auth_r_lock::test_auth(lock_data &l) const {
    if (!this->order_allowed(l.order)) return false;
    if (!l.read) return false;
    //(see 'lock_auth_rw_lock::test_auth' for comment)
    if (reading && (l.must_block || l.lock_out)) l.block = false;
    return true;
  }

  void lock_auth_r_lock::release_auth(unlock_data &l) {
    assert(l.read);
    assert(reading > 0);
    --reading;
  }


  lock_auth_w_lock::lock_auth_w_lock() : writing(0) {}

  lock_auth_w_lock::count_type lock_auth_w_lock::writing_count() const { return writing; }

  lock_auth_w_lock::~lock_auth_w_lock() {
    //NOTE: this can't be in '~lock_auth_base'!
    //NOTE: no point checking 'reading_count', since it's overrides will be ignored here
    assert(!this->writing_count());
  }

  bool lock_auth_w_lock::register_auth(lock_data &l) {
    if (!this->test_auth(l)) return false;
    ++writing;
    assert(writing > 0);
    return true;
  }

  bool lock_auth_w_lock::test_auth(lock_data &l) const {
    if (!this->order_allowed(l.order)) return false;
    //(see 'lock_auth_rw_lock::test_auth' for comment)
    if (writing && (l.must_block || l.lock_out)) l.block = false;
    return true;
  }

  void lock_auth_w_lock::release_auth(unlock_data &l) {
    assert(writing > 0);
    --writing;
  }


  lock_auth_dumb_lock::lock_auth_dumb_lock() : writing(false) {}

  lock_auth_dumb_lock::count_type lock_auth_dumb_lock::writing_count() const { return writing? 1 : 0; }

  lock_auth_dumb_lock::~lock_auth_dumb_lock() {
    //NOTE: this can't be in '~lock_auth_base'!
    //NOTE: no point checking 'reading_count', since it's overrides will be ignored here
    assert(!this->writing_count());
  }

  bool lock_auth_dumb_lock::register_auth(lock_data &l) {
    if (!this->test_auth(l)) return false;
    writing = true;
    return true;
  }

  bool lock_auth_dumb_lock::test_auth(lock_data &l) const {
    if (!this->order_allowed(l.order)) return false;
    return !writing;
  }

  void lock_auth_dumb_lock::release_auth(unlock_data &l) {
    assert(writing);
    writing = false;
  }


  bool lock_auth_broken_lock::register_auth(lock_data &l) {
    return false;
  }

  bool lock_auth_broken_lock::test_auth(lock_data &l) const {
    return false;
  }

  void lock_auth_broken_lock::release_auth(unlock_data &l) {
    assert(false);
  }


//multi-lock.hpp

  multi_lock_base::write_proxy multi_lock_base::get_write_auth(auth_type &authorization,
    bool block) {
    if (!authorization) return write_proxy();
    return this->get_write_auth(authorization.get(), block);
  }

} //namespace lc
