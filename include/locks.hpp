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

#ifndef lc_locks_hpp
#define lc_locks_hpp

#include <atomic>
#include <memory>

#include <assert.h>
#include <pthread.h>

#include "lock-auth.hpp"

namespace lc {


/*! \class lock_base
 *  \brief Base class for lock classes.
 */

class lock_base {
public:
  typedef lock_auth_base::count_type count_type;
  typedef lock_auth_base::order_type order_type;

  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual count_type lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) = 0;

  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual count_type unlock(lock_auth_base *auth, bool read, bool test = false) = 0;

  virtual order_type get_order() const;

protected:
  static inline bool register_or_test_auth(lock_auth_base *auth, lock_data &l, bool test_auth) {
    if (!auth) return true;
    return test_auth? auth->test_auth(l) : auth->register_auth(l);
  }

  static inline void release_auth(lock_auth_base *auth, unlock_data &l) {
    if (auth) auth->release_auth(l);
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

  rw_lock();

private:
  rw_lock(const rw_lock&);
  rw_lock &operator = (const rw_lock&);

public:
  count_type lock(lock_auth_base *auth, bool read, bool block = true, bool test = false);
  count_type unlock(lock_auth_base *auth, bool read, bool test = false);

  ~rw_lock();

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

  r_lock();

private:
  r_lock(const r_lock&);
  r_lock &operator = (const r_lock&);

public:
  count_type lock(lock_auth_base *auth, bool read, bool block = true, bool test = false);
  count_type unlock(lock_auth_base* auth, bool read, bool test = false);

  ~r_lock();

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

  w_lock();

private:
  w_lock(const w_lock&);
  w_lock &operator = (const w_lock&);

public:
  count_type lock(lock_auth_base *auth, bool read, bool block = true, bool test = false);

  count_type unlock(lock_auth_base *auth, bool read, bool test = false);

  ~w_lock();

private:
  bool            writer;
  count_type      writers_waiting;
  pthread_mutex_t master_lock;
  pthread_cond_t  write_wait;
};


/*! \class ordered_lock
 *  \brief Lock object that allows multiple readers at once.
 *
 * This lock is the same as Base (first template argument), except it requires
 * an order to be specified for the purposes of deadlock prevention. The lock
 * must be initialized with an order value >= 0 that dictates the order in which
 * objects must be locked when multiple locks are to be held at once. An order
 * of 0 means that the lock is unordered.
 * \attention This lock will not work with unordered auth. types. Better put,
 * unordered auth. types (e.g., lock_auth_rw_lock) won't authorize a lock on a
 * container with a ordered_lock.
 * \attention This lock will not allow locks without an auth. object.
 */

template <class Base = rw_lock>
class ordered_lock : public Base {
private:
  typedef Base base;

public:
  using typename base::count_type;
  using typename base::order_type;

  ordered_lock(order_type new_order) : order(new_order) {}

  count_type lock(lock_auth_base *auth, bool read, bool block = true, bool test = false) {
    if (!auth) return -1;
    return this->base::lock(auth, read, block, test);
  }

  count_type unlock(lock_auth_base *auth, bool read, bool test = false) {
    if (!auth) return -1;
    return this->base::unlock(auth, read, test);
  }

  virtual order_type get_order() const {
    return order;
  }

private:
  ordered_lock(const ordered_lock&);
  ordered_lock &operator = (const ordered_lock&);

  const order_type order;
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

  dumb_lock();

private:
  dumb_lock(const dumb_lock&);
  dumb_lock &operator = (const dumb_lock&);

public:
  count_type lock(lock_auth_base *auth, bool read, bool block = true, bool test = false);
  count_type unlock(lock_auth_base *auth, bool read, bool test = false);

  ~dumb_lock();

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

  count_type lock(lock_auth_base* auth, bool read, bool block = true, bool test = false);
  count_type unlock(lock_auth_base* auth, bool read, bool test = false);
};

} //namespace lc

#endif //lc_locks_hpp
