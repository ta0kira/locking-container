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

/* Container with automatic unlocking, concurrent reads, and deadlock prevention
 * (c) 2014-2015, Kevin P. Barry [ta0kira@gmail.com]
 *
 *
 * This file provides a template class that protects access to an object with
 * locks of various types. Conventional mutex-protection methods are susceptible
 * to bugs caused by forgetting (or neglecting) to lock/unlock the mutex. This
 * class eliminates the need to remember because the only access to the
 * protected object is via a proxy object that holds the lock until the
 * reference count of the proxy reaches 0 (like a shared pointer).
 *
 * This header contains one main class 'locking_container <class, class>', where
 * the first argument is the type of object being protected, and the second is
 * the type of lock to be used. A few lock choices are provided:
 *
 *   - 'rw_lock': This lock allows multiple readers at a time. This is the
 *     default lock used. A write lock can only be obtained if no other readers
 *     or writers have a lock. If a thread attempts to obtain a write lock and
 *     there are readers, it will block until all readers leave, blocking out
 *     all new readers and writers in the meantime. If 'lock_auth <rw_lock>'
 *     authorization is used (see below), the holder of the write lock can
 *     subsequently obtain a new read lock for the same container; otherwise,
 *     all read locks will be denied while the write lock is in place.
 *
 *   - 'r_lock': This lock allows multiple readers, but it never allows writers.
 *     This might be useful if you have a container that will never be written
 *     to but you nevertheless need to retain the same container semantics.
 *
 *   - 'w_lock': This lock doesn't make a distinction between readers and
 *     writers; only one thread can hold a lock at any given time. This should
 *     operate faster if you don't need read locks. Note that, for the purposes
 *     of deadlock prevention, this treats all locks as write locks.
 *
 *   - 'broken_lock': This is mostly a joke; however, you can use it to test
 *     pathological cases. This lock will always fail to lock and unlock.
 *
 * Each lock type has a corresponding 'lock_auth' specialization for use with
 * deadlock prevention. All of them have (somewhat) identical behavior to their
 * corresponding lock types, as far as how many read and write locks can be held
 * at a given time.
 *
 *   - 'lock_auth <rw_lock>': This auth. type allows the caller to hold multiple
 *     read locks, or a single write lock, but not both. Note that if another
 *     thread is waiting for a write lock on the container and the caller
 *     already has a read lock then the lock will be rejected. Two exceptions to
 *     these rules are: 1) if the container to be locked currently has no other
 *     locks; 2) if the call isn't blocking and it's for a write lock.
 *
 *   - 'lock_auth <r_lock>': This auth. type allows the caller to hold multiple
 *     read locks, but no write locks. Note that if another thread is waiting
 *     for a write lock on the container and the caller already has a read lock
 *     then the lock will be rejected. Use this auth. type if you want to ensure
 *     that a thread doesn't obtain a write lock on any container.
 *
 *   - 'lock_auth <w_lock>': This auth. type allows the caller to hold no more
 *     than one lock at a time, regardless of lock type. An exception to this
 *     behavior is if the container to be locked currently has no other locks.
 *     Use this auth. type if you are only using containers that use 'w_lock',
 *     or if you want to disallow multiple read locks on containers that use
 *     'rw_lock' or 'r_lock'.
 *
 *   - 'lock_auth <broken_lock>': This auth. type doesn't allow the caller to
 *     obtain any locks.
 *
 * If you want both deadlock prevention and the ability for threads to hold
 * a write lock plus one or more other locks at the same time, you can create a
 * 'null_container' for use by all threads when obtaining locks for any object.
 * The behavior will be transparent until a thread requests a "multi-lock" by
 * attempting to obtain a write lock on the 'null_container'. This will block
 * all future locks on the objects, allowing the thread in question to lock as
 * many objects as it needs to. To access this behavior, use 'get_multi' and
 * 'get_multi_const' instead of 'get_auth' and 'get_auth_const', passing the
 * 'null_container' as the first argument.
 *
 * Other notes:
 *
 *   - You must enable C++11 (or higher) when using this header.
 *
 *   - You might need to link your executable with libpthread after compiling.
 *
 *   - To copy a container, you must first get a proxy from it, then construct
 *     the copy with the corresponding object. To assign one container to
 *     another, you must first get proxies to both objects. (The
 *     'try_copy_container' global functions manage the latter automatically.)
 *     Because of this, you cannot copy a 'const' container because there is no
 *     'const' way to get a proxy.
 */


/*! \file locking-container.hpp
 *  \brief C++ container for data protection in multithreaded programs.
 *  \author Kevin P. Barry
 */

#ifndef locking_container_hpp
#define locking_container_hpp

#include <assert.h>

#include "locks.hpp"
#include "lock-auth.hpp"
#include "object-proxy.hpp"
#include "null-container.hpp"


/*! \class locking_container_base
    \brief Base class for \ref locking_container.
 */

template <class Type>
class locking_container_base {
public:
  typedef Type                      type;
  typedef object_proxy <type>       proxy;
  typedef object_proxy <const type> const_proxy;
  typedef lock_auth_base::auth_type auth_type;

  /** @name Accessor Functions
  *
  */
  //@{

  /*! \brief Retrieve a writable proxy to the contained object.
   *
   * @see object_proxy
   * \attention Always check that the returned object contains a valid
   * pointer with object_proxy::operator!. The reference will always be
   * invalid if a lock hasn't been obtained.
   * \attention The returned object should only be passed by value, and it
   * should only be passed within the same thread that
   * \ref locking_container::get was called from. This is because the proxy
   * object uses reference counting that isn't reentrant.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline proxy get(bool block = true) {
    return this->get_auth(NULL, block);
  }

  /*! \brief Retrieve a read-only proxy to the contained object.
   *
   * @see get
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline const_proxy get_const(bool block = true) {
    return this->get_auth_const(NULL, block);
  }

  /*! \brief Retrieve a writable proxy to the contained object using deadlock
   *  prevention.
   *
   * @see get
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline proxy get_auth(auth_type &authorization, bool block = true) {
    return this->get_auth(authorization.get(), block);
  }

  /*! \brief Retrieve a read-only proxy to the contained object using deadlock
   *  prevention.
   *
   * @see get
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline const_proxy get_auth_const(auth_type &authorization, bool block = true) {
    return this->get_auth_const(authorization.get(), block);
  }

  virtual proxy       get_auth(lock_auth_base *authorization, bool block = true)       = 0;
  virtual const_proxy get_auth_const(lock_auth_base *authorization, bool block = true) = 0;

  /*! \brief Retrieve a writable proxy to the contained object using deadlock
   *  prevention and multiple locking functionality.
   *
   * @see get_auth
   * \param multi_lock Multi-lock object to manage multiple locks.
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline proxy get_multi(null_container_base &multi_lock, lock_auth_base *authorization, bool block = true) {
    return this->get_multi(multi_lock.get_lock_object(), authorization, block);
  }

  /*! \brief Retrieve a read-only proxy to the contained object using deadlock
   *  prevention and multiple locking functionality.
   *
   * @see get_auth
   * \param multi_lock Multi-lock object to manage multiple locks.
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline const_proxy get_multi_const(null_container_base &multi_lock,
    lock_auth_base *authorization, bool block = true) {
    return this->get_multi_const(multi_lock.get_lock_object(), authorization, block);
  }

  /*! @see get_multi.*/
  inline proxy get_multi(null_container_base &multi_lock, auth_type &authorization,
    bool block = true) {
    return this->get_multi(multi_lock, authorization.get(), block);
  }

  /*! @see get_multi_const.*/
  inline const_proxy get_multi_const(null_container_base &multi_lock,
    auth_type &authorization, bool block = true) {
    return this->get_multi_const(multi_lock, authorization.get(), block);
  }

  //@}

  virtual auth_type get_new_auth() const {
    return auth_type();
  }

  virtual inline ~locking_container_base() {}

protected:
  virtual proxy get_multi(lock_base */*multi_lock*/, lock_auth_base */*authorization*/,
    bool /*block*/) {
    return proxy();
  }

  virtual const_proxy get_multi_const(lock_base */*multi_lock*/, lock_auth_base */*authorization*/,
    bool /*block*/) {
    return const_proxy();
  }
};


/*! \class locking_container
    \brief C++ container class with automatic unlocking, concurrent reads, and
    deadlock prevention.

    Each instance of this class contains a lock and an encapsulated object of
    the type denoted by the template parameter. The \ref locking_container::get
    and \ref locking_container::get_const functions provide a proxy object (see
    \ref object_proxy) that automatically locks and unlocks the lock to simplify
    code that accesses the encapsulated object.
 */

template <class Type, class Lock = rw_lock>
class locking_container : public locking_container_base <Type> {
private:
  typedef lock_auth <Lock> auth_base_type;

public:
  typedef locking_container_base <Type> base;
  using typename base::type;
  using typename base::proxy;
  using typename base::const_proxy;
  using typename base::auth_type;
  //NOTE: this is needed so that the 'lock_auth_base' variants are pulled in
  using base::get_auth;
  using base::get_auth_const;
  using base::get_multi;
  using base::get_multi_const;

  /*! \brief Constructor.
   *
   * \param Object Object to copy as contained object.
   */
  explicit locking_container(const type &Object = type()) : contained(Object) {}

private:
  locking_container(const locking_container&);
  locking_container &operator = (const locking_container&);

public:
  /** @name Accessor Functions
  *
  */
  //@{

  /*! @see locking_container_base::get_auth.*/
  inline proxy get_auth(lock_auth_base *authorization, bool block = true) {
    return this->get_multi(NULL, authorization, block);
  }

  /*! @see locking_container_base::get_auth_const.*/
  inline const_proxy get_auth_const(lock_auth_base *authorization, bool block = true) {
    return this->get_multi_const(NULL, authorization, block);
  }

  //@}

  /** @name New Authorization Objects
  *
  */
  //@{

  /*! Get a new authorization object.*/
  virtual auth_type get_new_auth() const {
    return locking_container::new_auth();
  }

  /*! Get a new authorization object.*/
  static auth_type new_auth() {
    return auth_type(new auth_base_type);
  }

  //@}

private:
  inline proxy get_multi(lock_base *multi_lock, lock_auth_base *authorization, bool block = true) {
    //NOTE: no read/write choice is given here!
    return proxy(&contained, &locks, authorization, block, multi_lock);
  }

  inline const_proxy get_multi_const(lock_base *multi_lock, lock_auth_base *authorization,
    bool block = true) {
    return const_proxy(&contained, &locks, authorization, true, block, multi_lock);
  }

  type contained;
  Lock locks;
};


/*! \brief Attempt to copy one container's contents into another.
 *
 * @note This will attempt to obtain locks for both containers, and will fail if
 * either lock operation fails.
 *
 * \param left container being assigned to
 * \param right container being assigned
 * \param authorization authorization object
 * \param block whether or not to block when locking the containers
 * \return success or failure, based entirely on locking success
 */
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &left,
  locking_container_base <Type2> &right, lock_auth_base *authorization,
  bool block = true) {
  typename locking_container_base <Type1> ::proxy write = left.get_auth(authorization, block);
  if (!write) return false;

  typename locking_container_base <Type2> ::const_proxy read = right.get_auth_const(authorization, block);
  if (!read) return false;

  *write = *read;
  return true;
}


/*! Attempt to copy one container's contents into another.*/
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &left,
  locking_container_base <Type2> &right, bool block = true) {
  return try_copy_container(left, right, NULL, block);
}


/*! Attempt to copy one container's contents into another.*/
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &left,
  locking_container_base <Type2> &right, lock_auth_base::auth_type &authorization,
  bool block = true) {
  return try_copy_container(left, right, authorization.get(), block);
}

/*! \brief Attempt to copy one container's contents into another.
 *
 * @note This will attempt to obtain locks for both containers using the
 * \ref null_container_base object, and will fail if either lock operation
 * fails.
 * \attention This will only work if no other thread holds a lock on either of
 * the containers.
 * \attention If Trymulti_lock is false, his will fail if the caller doesn't have a
 * write lock on the \ref null_container_base passed.
 *
 * \param left container being assigned to
 * \param right container being assigned
 * \param multi_lock multi-lock tracking object
 * \param authorization authorization object
 * \param block whether or not to block when locking the containers
 * \param Trymulti_lock whether or not to attempt a write lock on multi_lock
 * \return success or failure, based entirely on locking success
 */
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &left,
  locking_container_base <Type2> &right, null_container_base &multi_lock,
  lock_auth_base *authorization, bool block = true, bool Trymulti_lock = true) {
  null_container::proxy multi;
  if (Trymulti_lock && !(multi = multi_lock.get_auth(authorization, block))) return false;

  typename locking_container_base <Type1> ::proxy write = left.get_multi(multi_lock, authorization, block);
  if (!write) return false;

  typename locking_container_base <Type2> ::const_proxy read = right.get_multi_const(multi_lock, authorization, block);
  if (!read) return false;

  if (Trymulti_lock) multi.clear();

  *write = *read;
  return true;
}

/*! Attempt to copy one container's contents into another.*/
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &left,
  locking_container_base <Type2> &right, null_container_base &multi_lock,
  lock_auth_base::auth_type &authorization, bool block = true, bool Trymulti_lock = true) {
  return try_copy_container(left, right, multi_lock, authorization.get(), block, Trymulti_lock);
}

#endif //locking_container_hpp
