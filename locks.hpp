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


/*! \class lock_auth_base
    \brief Base class for lock authorization classes.
    @see lock_auth
 */

class lock_base;

class lock_auth_base {
public:
  typedef std::shared_ptr <lock_auth_base> auth_type;

  virtual int  reading_count() const { return 0; }
  virtual int  writing_count() const { return 0; }
  virtual bool always_read()   const { return false; }
  virtual bool always_write()  const { return false; }

  virtual inline ~lock_auth_base() {}

private:
  friend class lock_base;

  virtual bool register_auth(bool Read, bool LockOut, bool InUse, bool TestAuth) = 0;
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
  virtual int unlock(lock_auth_base *auth, bool read, bool test = false) = 0;

protected:
  static inline bool register_auth(lock_auth_base *auth, bool Read, bool LockOut,
    bool InUse, bool TestAuth) {
    return auth? auth->register_auth(Read, LockOut, InUse, TestAuth) : true;
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
  rw_lock() : readers(0), readers_waiting(0), writer(false), writer_waiting(false), the_writer(NULL) {
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
    bool writer_reads = auth && the_writer == auth && read;
    //make sure this is an authorized lock type for the caller
    if (!register_auth(auth, read, writer_reads? false : writer_waiting,
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
      int new_readers = ++readers;
      //if for some strange reason there's an overflow...
      assert((writer_reads || (!writer && !writer_waiting)) && readers > 0);
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
      the_writer = auth;
      pthread_mutex_unlock(&master_lock);
      return 0;
    }
  }

  int unlock(lock_auth_base *auth, bool read, bool test = false) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    if (!test) release_auth(auth, read);
    if (read) {
      assert(((auth && the_writer == auth) || !writer) && readers > 0);
      int new_readers = --readers;
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
    pthread_mutex_destroy(&master_lock);
    pthread_cond_destroy(&read_wait);
    pthread_cond_destroy(&write_wait);
  }

private:
  int              readers, readers_waiting;
  bool             writer, writer_waiting;
  const void      *the_writer;
  pthread_mutex_t  master_lock;
  pthread_cond_t   read_wait, write_wait;
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
    if (!register_auth(auth, read, false, false, test)) return -1;
    //NOTE: this should be atomic
    int new_counter = ++counter;
    //(check the copy!)
    assert(new_counter > 0);
    return new_counter;
  }

  int unlock(lock_auth_base *auth, bool read, bool test = false) {
    if (!read) return -1;
    if (!test) release_auth(auth, read);
    //NOTE: this should be atomic
    int new_counter = --counter;
    //(check the copy!)
    assert(new_counter >= 0);
    return new_counter;
  }

private:
  std::atomic <int> counter;
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
    if (!register_auth(auth, false, locked, locked, test)) return -1;
    if ((block? pthread_mutex_lock : pthread_mutex_trylock)(&write_lock) != 0) {
      if (!test) release_auth(auth, false);
      return -1;
    }
    assert(!locked);
    locked = true;
    return 0;
  }

  int unlock(lock_auth_base *auth, bool /*read*/, bool test = false) {
    if (!test) release_auth(auth, false);
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


/*! \class broken_lock
    \brief Lock object that is permanently broken.
 */

struct broken_lock : public lock_base {
  int lock(lock_auth_base* /*auth*/, bool /*read*/, bool /*block*/ = true,
    bool /*test*/ = false) { return -1; }
  int unlock(lock_auth_base* /*auth*/, bool /*read*/, bool /*test*/ = false) { return -1; }
};

#endif //locks_hpp
