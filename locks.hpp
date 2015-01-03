#ifndef locks_hpp
#define locks_hpp

#include <atomic>
#include <memory>

#include <assert.h>
#include <pthread.h>


/*! \class lock_auth_base
    \brief Base class for lock authorization classes.
    @see lock_auth
 */

class lock_base;

class lock_auth_base {
public:
  typedef std::shared_ptr <lock_auth_base> auth_type;

  virtual bool lock_allowed(bool Read, bool Block = true) const = 0;

  virtual inline ~lock_auth_base() {}

private:
  friend class lock_base;

  virtual bool register_auth(bool Read, bool Block, bool LockOut, bool InUse, bool TestAuth) = 0;
  virtual void release_auth(bool Read) = 0;
};


/*! \class lock_base
    \brief Base class for lock classes.
 */

class lock_base {
public:
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) = 0;
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int unlock(lock_auth_base *auth, bool read) = 0;

protected:
  static inline bool register_auth(lock_auth_base *auth, bool Read, bool Block,
    bool LockOut, bool InUse, bool TestAuth) {
    return auth? auth->register_auth(Read, Block, LockOut, InUse, TestAuth) : true;
  }

  static inline void release_auth(lock_auth_base *auth, bool Read) {
    if (auth) auth->release_auth(Read);
  }
};


/*! \class rw_lock
    \brief Lock object that allows multiple readers at once.
 */

class rw_lock : public lock_base {
public:
  rw_lock() : readers(0), readers_waiting(0), writer(false), writer_waiting(false) {
    pthread_mutex_init(&master_lock, NULL);
    pthread_cond_init(&read_wait, NULL);
    pthread_cond_init(&write_wait, NULL);
  }

private:
  rw_lock(const rw_lock&);
  rw_lock &operator = (const rw_lock&);

public:
  int lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    //make sure this is an authorized lock type for the caller
    if (!register_auth(auth, read, block, writer_waiting, writer || readers, test)) {
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    //check for blocking behavior
    bool must_block = writer || writer_waiting || (!read && readers);
    if (!block && must_block) {
      if (!test) release_auth(auth, read);
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    if (read) {
      //get a read lock
      ++readers_waiting;
      //NOTE: 'auth' is expected to prevent a deadlock if the caller already has
      //a read lock and there is a writer waiting
      while (writer || writer_waiting) {
        if (pthread_cond_wait(&read_wait, &master_lock) != 0) {
          if (!test) release_auth(auth, read);
          --readers_waiting;
          pthread_mutex_unlock(&master_lock);
          return -1;
        }
      }
      --readers_waiting;
      int new_readers = ++readers;
      //if for some strange reason there's an overflow...
      assert(!writer && !writer_waiting && readers > 0);
      pthread_mutex_unlock(&master_lock);
      return new_readers;
    } else {
      //if the caller isn't the first in line for writing, wait until it is
      ++readers_waiting;
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
      pthread_mutex_unlock(&master_lock);
      return 0;
    }
  }

  int unlock(lock_auth_base *auth, bool read) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    release_auth(auth, read);
    if (read) {
      assert(!writer && readers > 0);
      int new_readers = --readers;
      if (!new_readers && writer_waiting) {
        pthread_cond_broadcast(&write_wait);
      }
      pthread_mutex_unlock(&master_lock);
      return new_readers;
    } else {
      assert(writer && !readers);
      writer = false;
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
    pthread_mutex_destroy(&master_lock);
    pthread_cond_destroy(&read_wait);
    pthread_cond_destroy(&write_wait);
  }

private:
  int             readers, readers_waiting;
  bool            writer, writer_waiting;
  pthread_mutex_t master_lock;
  pthread_cond_t  read_wait, write_wait;
};


/*! \class w_lock
    \brief Lock object that allows only one thread access at a time.
 */

class w_lock : public lock_base {
public:
  w_lock() : locked(false) {
    pthread_mutex_init(&write_lock, NULL);
  }

private:
  w_lock(const w_lock&);
  w_lock &operator = (const w_lock&);

public:
  int lock(lock_auth_base *auth, bool /*read*/, bool block = true, bool test = false) {
    //NOTE: 'false' is passed instead of 'read' because this can lock out other readers
    if (!register_auth(auth, false, block, locked, locked, test)) return -1;
    if ((block? pthread_mutex_lock : pthread_mutex_trylock)(&write_lock) != 0) {
      if (!test) release_auth(auth, false);
      return -1;
    }
    assert(!locked);
    locked = true;
    return 0;
  }

  int unlock(lock_auth_base *auth, bool /*read*/) {
    release_auth(auth, false);
    assert(locked);
    locked = false;
    return (pthread_mutex_unlock(&write_lock) == 0)? 0 : -1;
  }

  ~w_lock() {
    pthread_mutex_destroy(&write_lock);
  }

private:
  bool locked;
  pthread_mutex_t write_lock;
};


/*! \class r_lock
    \brief Lock object that allows multiple readers but no writers.
 */

class r_lock : public lock_base {
public:
  r_lock() : counter(0) {}

private:
  r_lock(const r_lock&);
  r_lock &operator = (const r_lock&);

public:
  int lock(lock_auth_base *auth, bool read, bool /*block*/ = true, bool test = false) {
    if (!read) return -1;
    if (!register_auth(auth, read, false, false, false, test)) return -1;
    //NOTE: this should be atomic
    int new_counter = ++counter;
    //(check the copy!)
    assert(new_counter > 0);
    return new_counter;
  }

  int unlock(lock_auth_base *auth, bool read) {
    if (!read) return -1;
    release_auth(auth, read);
    //NOTE: this should be atomic
    int new_counter = --counter;
    //(check the copy!)
    assert(new_counter >= 0);
    return new_counter;
  }

private:
  std::atomic <int> counter;
};


/*! \class broken_lock
    \brief Lock object that is permanently broken.
 */

struct broken_lock : public lock_base {
  int lock(lock_auth_base* /*auth*/, bool /*read*/, bool /*block*/ = true,
    bool /*test*/ = false) { return -1; }
  int unlock(lock_auth_base* /*auth*/, bool /*read*/) { return -1; }
};

#endif //locks_hpp
