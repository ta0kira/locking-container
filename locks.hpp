/* This software is released under the BSD License.
 |
 | Copyright (c) 2015, Kevin P. Barry [ta0kira@gmail.com]
 | All rights reserved.
 |
 | Redistribution  and  use  in  source  and   binary  forms,  with  or  without
 | modification, are permitted provided that the following conditions are met:
 |
 | - Redistributions of source code must retain the above copyright notice, this
 |   list of conditions and the following disclaimer.
 |
 | - Redistributions in binary  form must reproduce the  above copyright notice,
 |   this list  of conditions and the following disclaimer in  the documentation
 |   and/or other materials provided with the distribution.
 |
 | - Neither the name  of the  Locking Container Project  nor  the names  of its
 |   contributors may be  used to endorse or promote products  derived from this
 |   software without specific prior written permission.
 |
 | THIS SOFTWARE IS  PROVIDED BY THE COPYRIGHT HOLDERS AND  CONTRIBUTORS "AS IS"
 | AND ANY  EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT LIMITED TO, THE
 | IMPLIED WARRANTIES OF  MERCHANTABILITY  AND FITNESS FOR A  PARTICULAR PURPOSE
 | ARE DISCLAIMED.  IN  NO EVENT SHALL  THE COPYRIGHT  OWNER  OR CONTRIBUTORS BE
 | LIABLE  FOR  ANY  DIRECT,   INDIRECT,  INCIDENTAL,   SPECIAL,  EXEMPLARY,  OR
 | CONSEQUENTIAL   DAMAGES  (INCLUDING,  BUT  NOT  LIMITED  TO,  PROCUREMENT  OF
 | SUBSTITUTE GOODS OR SERVICES;  LOSS  OF USE,  DATA,  OR PROFITS;  OR BUSINESS
 | INTERRUPTION)  HOWEVER  CAUSED  AND ON  ANY  THEORY OF LIABILITY,  WHETHER IN
 | CONTRACT,  STRICT  LIABILITY, OR  TORT (INCLUDING  NEGLIGENCE  OR  OTHERWISE)
 | ARISING IN ANY  WAY OUT OF  THE USE OF THIS SOFTWARE, EVEN  IF ADVISED OF THE
 | POSSIBILITY OF SUCH DAMAGE.
 +~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#ifndef locks_hpp
#define locks_hpp

#include <atomic>
#include <memory>

#include <assert.h>
#include <pthread.h>

#include "lock-auth.hpp"


/*! \class lock_base
 *  \brief Base class for lock classes.
 */

class lock_base {
public:
  typedef lock_auth_base::count_type count_type;

  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual count_type lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) = 0;

  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual count_type unlock(lock_auth_base *auth, bool read, bool test = false) = 0;

protected:
  static inline bool register_or_test_auth(lock_auth_base *auth, bool read, bool lock_out,
    bool in_use, bool test_auth) {
    if (!auth) return true;
    return test_auth? auth->test_auth(read, lock_out, in_use) :
                      auth->register_auth(read, lock_out, in_use);
  }

  static inline void release_auth(lock_auth_base *auth, bool read) {
    if (auth) auth->release_auth(read);
  }
};


/*! \class rw_lock
 *  \brief Lock object that allows multiple readers at once.
 *
 * This lock allows multiple readers at a time. This is the default lock used.
 * A write lock can only be obtained if no other readers or writers have a lock.
 * If a thread attempts to obtain a write lock and there are readers, it will
 * block until all readers leave, blocking out all new readers and writers in
 * the meantime. If 'lock_auth <rw_lock>' authorization is used (see below), the
 * holder of the write lock can subsequently obtain a new read lock for the same
 * container; otherwise, all read locks will be denied while the write lock is
 * in place.
 */

class rw_lock : public lock_base {
public:
  using lock_base::count_type;

  rw_lock() : readers(0), readers_waiting(0), writer(false), writer_waiting(false), the_writer(NULL) {
    pthread_mutex_init(&master_lock, NULL);
    pthread_cond_init(&read_wait, NULL);
    pthread_cond_init(&write_wait, NULL);
  }

private:
  rw_lock(const rw_lock&);
  rw_lock &operator = (const rw_lock&);

public:
  long lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    bool writer_reads = auth && the_writer == auth && read;
    //make sure this is an authorized lock type for the caller
    if (!register_or_test_auth(auth, read, writer_reads? false : writer_waiting,
                               writer_reads? false : (writer || readers), test)) {
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    //check for blocking behavior
    bool must_block = writer || writer_waiting || (!read && readers);
    //exception to blocking: if 'auth' holds the write lock and a read is requested
    if (!writer_reads && !block && must_block) {
      if (!test) release_auth(auth, read);
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
          if (!test) release_auth(auth, read);
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
          if (!test) release_auth(auth, read);
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
          if (!test) release_auth(auth, read);
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

  count_type unlock(lock_auth_base *auth, bool read, bool test = false) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    if (!test) release_auth(auth, read);
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

  ~rw_lock() {
    assert(!readers && !readers_waiting && !writer && !writer_waiting);
    pthread_mutex_destroy(&master_lock);
    pthread_cond_destroy(&read_wait);
    pthread_cond_destroy(&write_wait);
  }

private:
  count_type       readers, readers_waiting;
  bool             writer, writer_waiting;
  const void      *the_writer;
  pthread_mutex_t  master_lock;
  pthread_cond_t   read_wait, write_wait;
};


/*! \class r_lock
 *  \brief Lock object that allows multiple readers but no writers.
 *
 * This lock allows multiple readers, but it never allows writers. This might be
 * useful if you have a container that will never be written to but you
 * nevertheless need to retain the same container semantics.
 */

class r_lock : public lock_base {
public:
  using lock_base::count_type;

  r_lock() : readers(0) {}

private:
  r_lock(const r_lock&);
  r_lock &operator = (const r_lock&);

public:
  count_type lock(lock_auth_base *auth, bool read, bool /*block*/ = true, bool /*test*/ = false) {
    if (!read) return -1;
    //NOTE: because this container can't be a part of a deadlock, it's never
    //considered in use and the lock isn't counted. the 'auth' check is entirely
    //to allow for an auth. that denies all locks.
    if (!register_or_test_auth(auth, true, false, false, true)) return -1;
    //NOTE: this is atomic
    count_type new_readers = ++readers;
    //(check the copy!)
    assert(new_readers > 0);
    return new_readers;
  }

  count_type unlock(lock_auth_base */*auth*/, bool read, bool /*test*/ = false) {
    if (!read) return -1;
    //NOTE: this is atomic
    count_type new_readers = --readers;
    //(check the copy!)
    assert(new_readers >= 0);
    return new_readers;
  }

  ~r_lock() {
    assert(!readers);
  }

private:
  std::atomic <count_type> readers;
};


/*! \class w_lock
 *  \brief Lock object that allows only one thread access at a time.
 *
 * This lock doesn't make a distinction between readers and writers; only one
 * thread can hold a lock at any given time. This should operate faster if you
 * don't need read locks. Note that, for the purposes of deadlock prevention,
 * this treats all locks as write locks.
 */

class w_lock : public lock_base {
public:
  using lock_base::count_type;

  w_lock() : writer(false), writers_waiting(0) {
    pthread_mutex_init(&master_lock, NULL);
    pthread_cond_init(&write_wait, NULL);
  }

private:
  w_lock(const w_lock&);
  w_lock &operator = (const w_lock&);

public:
  count_type lock(lock_auth_base *auth, bool /*read*/, bool block = true, bool test = false) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    //NOTE: 'false' is passed instead of 'read' because this can lock out other readers
    if (!register_or_test_auth(auth, false, writer, writer, test)) {
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    if (!block && writer) {
      if (!test) release_auth(auth, false);
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    ++writers_waiting;
    assert(writers_waiting > 0);
    while (writer) {
      if (pthread_cond_wait(&write_wait, &master_lock) != 0) {
        if (!test) release_auth(auth, false);
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

  count_type unlock(lock_auth_base *auth, bool /*read*/, bool test = false) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    if (!test) release_auth(auth, false);
    assert(writer);
    writer = false;
    if (writers_waiting) {
      pthread_cond_broadcast(&write_wait);
    }
    pthread_mutex_unlock(&master_lock);
    return 0;
  }

  ~w_lock() {
    assert(!writer && !writers_waiting);
    pthread_mutex_destroy(&master_lock);
    pthread_cond_destroy(&write_wait);
  }

private:
  bool            writer;
  count_type      writers_waiting;
  pthread_mutex_t master_lock;
  pthread_cond_t  write_wait;
};


/*! \class dumb_lock
 *  \brief Lock object that doesn't track readers and writers.
 *
 * This is the simplest lock class. It doesn't track the number of readers and
 * writers; therefore, it always assumes that the container is in use and/or
 * the lock will block (for the purposes of lock authorization). If lock
 * authorization is used, this lock will almost certainly reject a lock if the
 * caller holds any other locks. This means that a container using this type of
 * lock cannot be a part of a multi-lock operation.
 */

class dumb_lock : public lock_base {
public:
  using lock_base::count_type;

  dumb_lock() {
    pthread_mutex_init(&master_lock, NULL);
  }

private:
  dumb_lock(const dumb_lock&);
  dumb_lock &operator = (const dumb_lock&);

public:
  count_type lock(lock_auth_base *auth, bool /*read*/, bool block = true, bool test = false) {
    if (!register_or_test_auth(auth, false, true, true, test)) return -1;
    if ((block? pthread_mutex_lock : pthread_mutex_trylock)(&master_lock) != 0) {
      if (!test) release_auth(auth, false);
      return -1;
    }
    return 0;
  }

  count_type unlock(lock_auth_base *auth, bool /*read*/, bool test = false) {
    if (!test) release_auth(auth, false);
    return (pthread_mutex_unlock(&master_lock) == 0)? 0 : -1;
  }

  ~dumb_lock() {
    //NOTE: this is the only reasonable way to see if there is currently a lock
    assert(pthread_mutex_trylock(&master_lock) == 0);
    pthread_mutex_destroy(&master_lock);
  }

private:
  pthread_mutex_t master_lock;
};


/*! \class broken_lock
 *  \brief Lock object that is permanently broken.
 *
 * This is mostly a joke; however, you can use it to test pathological cases.
 * This lock will always fail to lock and unlock.
 */

struct broken_lock : public lock_base {
  using lock_base::count_type;

  count_type lock(lock_auth_base* /*auth*/, bool /*read*/, bool /*block*/ = true,
    bool /*test*/ = false) { return -1; }
  count_type unlock(lock_auth_base* /*auth*/, bool /*read*/, bool /*test*/ = false) { return -1; }
};

#endif //locks_hpp
