/* This software is released under the BSD License.
 |
 | Copyright (c) 2014-2015, Kevin P. Barry [ta0kira@gmail.com]
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

/* This file provides a template class that protects access to an object with
 * locks of various types. Conventional mutex-protection methods are susceptible
 * to bugs caused by forgetting (or neglecting) to lock/unlock the mutex. This
 * class eliminates the need to remember because the only access to the
 * protected object is via a proxy object that holds the lock until the
 * reference count of the proxy reaches 0 (like a shared pointer).
 *
 * This header contains one main class 'locking_container <class, class>', where
 * the first argument is the type of object being protected, and the second is
 * the type of lock to be used. A few lock choices are provided. See
 * locks.hpp for more information.
 *
 * Each lock type has a corresponding 'lock_auth' specialization for use with
 * deadlock prevention. All of them have (somewhat) identical behavior to their
 * corresponding lock types, as far as how many read and write locks can be held
 * at a given time. See lock-auth.hpp for more information.
 *
 * There are two alternatives for accessing multiple containers for writing when
 * using deadlock prevention. Note that these only work all of your code uses
 * the same strategy; deadlocking won't be prevented otherwise.
 *
 *   - Use multi-locking, which requires a global 'multi_lock' for use by
 *     all threads when obtaining locks for any object. This functions by
 *     keeping track of the locks on all objects. When a thread wants to hold
 *     multiple write locks at once, it blocks for a write lock on the
 *     'multi_lock', which ensures that it will have exclusive access to all
 *     objects while it still holds that write lock.
 *
 *   - Use ordered_lock as the lock type for your containers. This type of lock
 *     enforces a strict locking order when multiple locks are to be obtained.
 *     Each container is assigned an order, and a thread is only authorized to
 *     wait for a lock on a container that has a higher order than the highest
 *     container order the thread already has a lock for. This is much more
 *     efficient than the strategy above, but it requires that you establish an
 *     order for all containers and that you respect that ordering when
 *     obtaining multiple locks.
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

#ifndef lc_locking_container_hpp
#define lc_locking_container_hpp

#include <utility>

#include "locks.hpp"
#include "lock-auth.hpp"
#include "object-proxy.hpp"
#include "meta-lock.hpp"

namespace lc {


/*! \class locking_container_base
 *  \brief Base class for \ref locking_container.
 *
 * \attention The object being protected should have no side-effects when a
 * const function is called (e.g., by calling non-reentrant C functions). This
 * is because some containers grant multiple read locks at one time, which means
 * that side effects could cause problems in multithreaded code.
 */

template <class Type>
class locking_container_base {
public:
  typedef Type                       type;
  typedef object_proxy <type>        write_proxy;
  typedef object_proxy <const type>  read_proxy;
  typedef lock_auth_base::auth_type  auth_type;
  typedef lock_auth_base::order_type order_type;

  /** @name Accessor Functions
   *
   */
  //@{

  /*! \brief Retrieve a writable proxy to the contained object.
   *
   * @see object_proxy
   * \attention Always check that the returned object contains a valid pointer
   * with object_proxy::operator!. The pointer will always be NULL if a lock
   * hasn't been obtained.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline write_proxy get_write(bool block = true) {
    return this->get_write_auth(NULL, block);
  }

  /*! \brief Retrieve a read-only proxy to the contained object.
   *
   * @see get_write
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline read_proxy get_read(bool block = true) {
    return this->get_read_auth(NULL, block);
  }

  /*! \brief Retrieve a writable proxy to the contained object using deadlock
   *  prevention.
   *
   * @see get_write
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline write_proxy get_write_auth(auth_type &authorization, bool block = true) {
    if (!authorization) return write_proxy();
    return this->get_write_auth(authorization.get(), block);
  }

  /*! \brief Retrieve a read-only proxy to the contained object using deadlock
   *  prevention.
   *
   * @see get_write
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline read_proxy get_read_auth(auth_type &authorization, bool block = true) {
    if (!authorization) return read_proxy();
    return this->get_read_auth(authorization.get(), block);
  }

  /*! \brief Retrieve a writable proxy to the contained object using deadlock
   *  prevention and multiple locking functionality.
   *
   * @see get_write_auth
   * \param meta_lock Multi-lock object to manage multiple locks.
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline write_proxy get_write_multi(meta_lock_base &meta_lock,
    auth_type &authorization, bool block = true) {
    if (!authorization) return write_proxy();
    return this->get_write_multi(meta_lock.get_lock_object(), authorization.get(), block);
  }

  /*! \brief Retrieve a read-only proxy to the contained object using deadlock
   *  prevention and multiple locking functionality.
   *
   * @see get_write_auth
   * \param meta_lock Multi-lock object to manage multiple locks.
   * \param authorization Authorization object to prevent deadlocks.
   * \param block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline read_proxy get_read_multi(meta_lock_base &meta_lock,
    auth_type &authorization, bool block = true) {
    if (!authorization) return read_proxy();
    return this->get_read_multi(meta_lock.get_lock_object(), authorization.get(), block);
  }

  //@}

  /*! Get a new authorization object.*/
  virtual auth_type get_new_auth() const {
    return auth_type();
  }

  /*! Get the container's order.*/
  virtual order_type get_order() const {
    return 0;
  }

  virtual inline ~locking_container_base() {}

protected:
  virtual write_proxy get_write_auth(lock_auth_base *authorization, bool block) = 0;
  virtual read_proxy  get_read_auth(lock_auth_base *authorization, bool block)  = 0;

  virtual write_proxy get_write_multi(lock_base* /*meta_lock*/,
    lock_auth_base* /*authorization*/, bool /*block*/) {
    return write_proxy();
  }

  virtual read_proxy get_read_multi(lock_base* /*meta_lock*/,
    lock_auth_base* /*authorization*/, bool /*block*/) {
    return read_proxy();
  }
};


/*! \class locking_container
 *  \brief C++ container class with automatic unlocking, concurrent reads, and
 *  deadlock prevention.
 *
 * Each instance of this class contains a lock and an encapsulated object of
 * the type denoted by the template parameter. The
 * \ref locking_container::get_write and \ref locking_container::get_read
 * functions provide a proxy object (see \ref object_proxy) that automatically
 * locks and unlocks the lock to simplify code that accesses the encapsulated
 * object.
 */

template <class Type, class Lock = rw_lock>
class locking_container : public locking_container_base <Type> {
private:
  typedef lock_auth <Lock> auth_base_type;

public:
  typedef locking_container_base <Type> base;
  using typename base::type;
  using typename base::write_proxy;
  using typename base::read_proxy;
  using typename base::auth_type;
  using typename base::order_type;
  //NOTE: this is needed so that the 'lock_auth_base' variants are pulled in
  using base::get_write_auth;
  using base::get_read_auth;
  using base::get_write_multi;
  using base::get_read_multi;

  /*! \brief Constructor.
   *
   * \param object object to copy as contained object.
   */
  explicit locking_container() : contained() {}

  /*! \brief Constructor.
   *
   * \param object object to copy as contained object.
   * \param args arguments to pass to the lock's constructor.
   */
  template <class ... Types>
  explicit locking_container(type &&object, Types ... args) : contained(std::move(object)), locks(args...) {}

  /*! \brief Constructor.
   *
   * \param object object to copy as contained object.
   * \param args arguments to pass to the lock's constructor.
   */
  template <class ... Types>
  explicit locking_container(const type &object, Types ... args) : contained(object), locks(args...) {}

private:
  locking_container(const locking_container&);
  locking_container &operator = (const locking_container&);

public:
  /** @name Authorization
   *
   */
  //@{

  /*! Get a new authorization object.*/
  auth_type get_new_auth() const {
    return locking_container::new_auth();
  }

  /*! Get a new authorization object.*/
  static auth_type new_auth() {
    return auth_type(new auth_base_type);
  }

  /*! Get the container's order.*/
  order_type get_order() const {
    return locks.get_order();
  }

  //@}

private:
  inline write_proxy get_write_auth(lock_auth_base *authorization, bool block) {
    return this->get_write_multi(NULL, authorization, block);
  }

  inline read_proxy get_read_auth(lock_auth_base *authorization, bool block) {
    return this->get_read_multi(NULL, authorization, block);
  }

  inline write_proxy get_write_multi(lock_base *meta_lock, lock_auth_base *authorization, bool block) {
    //NOTE: no read/write choice is given here!
    return write_proxy(&contained, &locks, authorization, false, block, meta_lock);
  }

  inline read_proxy get_read_multi(lock_base *meta_lock, lock_auth_base *authorization,
    bool block) {
    return read_proxy(&contained, &locks, authorization, true, block, meta_lock);
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
 * \param block whether or not to block when locking the containers
 * \return success or failure, based entirely on locking success
 */
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &left,
  locking_container_base <Type2> &right, bool block = true) {
  typename locking_container_base <Type1> ::write_proxy write = left.get_write(block);
  if (!write) return false;

  typename locking_container_base <Type2> ::read_proxy read = right.get_read(block);
  if (!read) return false;

  *write = *read;
  return true;
}


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
  locking_container_base <Type2> &right, lock_auth_base::auth_type &authorization,
  bool block = true) {
  typename locking_container_base <Type1> ::write_proxy write;
  typename locking_container_base <Type2> ::read_proxy  read;

  //NOTE: if either is 0, the order is arbitrary
  if (left.get_order() < right.get_order()) {
    write = left.get_write_auth(authorization, block);
    read  = right.get_read_auth(authorization, block);
  } else {
    read  = right.get_read_auth(authorization, block);
    write = left.get_write_auth(authorization, block);
  }

  if (!write || !read) return false;

  *write = *read;
  return true;
}

/*! \brief Attempt to copy one container's contents into another.
 *
 * @note This will attempt to obtain locks for both containers using the
 * \ref meta_lock_base object, and will fail if either lock operation
 * fails.
 * \attention This will only work if no other thread holds a lock on either of
 * the containers.
 * \attention If try_multi is false, his will fail if the caller doesn't have a
 * write lock on the \ref meta_lock_base passed.
 *
 * \param left container being assigned to
 * \param right container being assigned
 * \param meta_lock multi-lock tracking object
 * \param authorization authorization object
 * \param block whether or not to block when locking the containers
 * \param try_multi whether or not to attempt a write lock on meta_lock
 * \return success or failure, based entirely on locking success
 */
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &left,
  locking_container_base <Type2> &right, meta_lock_base &master_lock,
  lock_auth_base::auth_type &authorization, bool block = true, bool try_multi = true) {
  meta_lock::write_proxy multi;
  if (try_multi && !(multi = master_lock.get_write_auth(authorization, block))) return false;

  typename locking_container_base <Type1> ::write_proxy write;
  typename locking_container_base <Type2> ::read_proxy  read;

  //NOTE: not entirely necessary with multi-locking, but we'll play the game

  //NOTE: if either is 0, the order is arbitrary
  if (left.get_order() < right.get_order()) {
    write = left.get_write_multi(master_lock, authorization, block);
    read  = right.get_read_multi(master_lock, authorization, block);
  } else {
    read  = right.get_read_multi(master_lock, authorization, block);
    write = left.get_write_multi(master_lock, authorization, block);
  }

  if (try_multi) multi.clear();

  if (!write || !read) return false;

  *write = *read;
  return true;
}

} //namespace lc

#endif //lc_locking_container_hpp
