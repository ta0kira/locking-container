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
 *     all new readers and writers in the meantime.
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
 * deadlock prevention. All of them have (nearly) identical behavior to their
 * corresponding lock types, as far as how many read and write locks can be held
 * at a given time.
 *
 *   - 'lock_auth <rw_lock>': This auth. type allows the caller to hold multiple
 *     read locks, or a single write lock, but not both. Note that if another
 *     thread is waiting for a write lock on the container and the caller
 *     already has a read lock then the lock will be rejected. Two exception to
 *     these rules are: if the container to be locked currently has no other
 *     locks, or if the call isn't blocking and it's for a write lock.
 *
 *   - 'lock_auth <r_lock>': This auth. type allows the caller to hold multiple
 *     read locks, but no write locks. Note that if another thread is waiting
 *     for a write lock on the container and the caller already has a read lock
 *     then the lock will be rejected.
 *
 *   - 'lock_auth <w_lock>': This auth. type allows the caller to hold no more
 *     than one lock at a time, regardless of lock type. This is the default
 *     behavior of 'lock_auth' when it isn't specialized for a lock type. An
 *     exception to this behavior is if the container to be locked currently has
 *     no other locks.
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
 *     another, you must first get proxies to both objects. Because of this,
 *     you cannot copy a 'const' container because there is no 'const' way to
 *     get a proxy.
 */


/*! \file locking-container.hpp
 *  \brief C++ container for data protection in multithreaded programs.
 *  \author Kevin P. Barry
 */

#ifndef locking_container_hpp
#define locking_container_hpp

#include <assert.h>

#include "locks.hpp"
#include "lock_auth.hpp"
#include "object_proxy.hpp"
#include "null_container.hpp"


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

  inline proxy get(bool Block = true) {
    return this->get_auth(NULL, Block);
  }

  inline const_proxy get_const(bool Block = true) {
    return this->get_auth_const(NULL, Block);
  }

  inline proxy get_auth(auth_type &Authorization, bool Block = true) {
    return this->get_auth(Authorization.get(), Block);
  }

  inline const_proxy get_auth_const(auth_type &Authorization, bool Block = true) {
    return this->get_auth_const(Authorization.get(), Block);
  }

  virtual proxy       get_auth(lock_auth_base *Authorization, bool Block = true)       = 0;
  virtual const_proxy get_auth_const(lock_auth_base *Authorization, bool Block = true) = 0;

  inline proxy get_multi(null_container_base &Multi, lock_auth_base *Authorization, bool Block = true) {
    return this->get_multi(Multi.get_lock_object(), Authorization, Block);
  }

  inline const_proxy get_multi_const(null_container_base &Multi,
    lock_auth_base *Authorization, bool Block = true) {
    return this->get_multi_const(Multi.get_lock_object(), Authorization, Block);
  }

  inline proxy get_multi(null_container_base &Multi, auth_type &Authorization,
    bool Block = true) {
    return this->get_multi(Multi, Authorization.get(), Block);
  }

  inline const_proxy get_multi_const(null_container_base &Multi,
    auth_type &Authorization, bool Block = true) {
    return this->get_multi_const(Multi, Authorization.get(), Block);
  }

  virtual auth_type get_new_auth() const {
    return auth_type();
  }

  virtual inline ~locking_container_base() {}

protected:
  virtual proxy get_multi(lock_base */*Multi*/, lock_auth_base */*Authorization*/,
    bool /*Block*/) {
    return proxy();
  }

  virtual const_proxy get_multi_const(lock_base */*Multi*/, lock_auth_base */*Authorization*/,
    bool /*Block*/) {
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
  /*! \brief Destructor.
   *
   * \attention This will block if the container is locked.
   */
  ~locking_container() {
    this->get();
  }

  /** @name Accessor Functions
  *
  */
  //@{

  /*! \brief Retrieve a proxy to the contained object.
   *
   * @see object_proxy
   * \attention Always check that the returned object contains a valid
   * pointer with object_proxy::operator!. The reference will always be
   * invalid if a lock hasn't been obtained.
   * \attention The returned object should only be passed by value, and it
   * should only be passed within the same thread that
   * \ref locking_container::get was called from. This is because the proxy
   * object uses reference counting that isn't reentrant.
   * \param Authorization Authorization object to prevent deadlocks.
   * \param Block Should the call block for a lock?
   *
   * \return proxy object
   */
  inline proxy get_auth(lock_auth_base *Authorization, bool Block = true) {
    return this->get_multi(NULL, Authorization, Block);
  }

  /*! Const version of \ref locking_container::get.*/
  inline const_proxy get_auth_const(lock_auth_base *Authorization, bool Block = true) {
    return this->get_multi_const(NULL, Authorization, Block);
  }

  //@}

  /*! Get a new authorization object.*/
  virtual auth_type get_new_auth() const {
    return locking_container::new_auth();
  }

  /*! Get a new authorization object.*/
  static auth_type new_auth() {
    return auth_type(new auth_base_type);
  }

private:
  inline proxy get_multi(lock_base *Multi, lock_auth_base *Authorization, bool Block = true) {
    //NOTE: no read/write choice is given here!
    return proxy(&contained, &locks, Authorization, Block, Multi);
  }

  inline const_proxy get_multi_const(lock_base *Multi, lock_auth_base *Authorization,
    bool Block = true) {
    return const_proxy(&contained, &locks, Authorization, true, Block, Multi);
  }

  type contained;
  Lock locks;
};


/*! \brief Attempt to copy one container's contents into another.
 *
 * @note This will attempt to obtain locks for both objects, and will fail if
 * either lock operation fails.
 *
 * \param Left container being assigned to
 * \param Right container being assigned
 * \param Authorization authorization object
 * \param Block whether or not to block when locking the containers
 * \return success or failure, based entirely on locking success
 */
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &Left,
  locking_container_base <Type2> &Right, lock_auth_base *Authorization,
  bool Block = true) {
  typename locking_container_base <Type1> ::proxy write = Left.get_auth(Authorization, Block);
  if (!write) return false;
  typename locking_container_base <Type2> ::const_proxy read = Right.get_auth_const(Authorization, Block);
  if (!read) return false;
  *write = *read;
  return true;
}


/*! Attempt to copy one container's contents into another.*/
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &Left,
  locking_container_base <Type2> &Right, bool Block = true) {
  return try_copy_container(Left, Right, NULL, Block);
}


/*! Attempt to copy one container's contents into another.*/
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &Left,
  locking_container_base <Type2> &Right, lock_auth_base::auth_type &Authorization,
  bool Block = true) {
  return try_copy_container(Left, Right, Authorization.get(), Block);
}

/*! \brief Attempt to copy one container's contents into another.
 *
 * @note This will attempt to get a write lock for the multi-lock object, and
 * also locks for both objects. This will fail if any of lock operation fails.
 * Specifically, this will fail if the auth. object holds any other locks.
 *
 * \param Left container being assigned to
 * \param Right container being assigned
 * \param Multi multi-lock tracking object
 * \param Authorization authorization object
 * \param Block whether or not to block when locking the containers
 * \return success or failure, based entirely on locking success
 */
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &Left,
  locking_container_base <Type2> &Right, null_container_base &Multi,
  lock_auth_base *Authorization, bool Block = true) {
  null_container_base::proxy multi = Multi.get_auth(Authorization);
  if (!multi) return false;
  typename locking_container_base <Type1> ::proxy write = Left.get_auth(Authorization, Block);
  if (!write) return false;
  typename locking_container_base <Type2> ::const_proxy read = Right.get_auth_const(Authorization, Block);
  if (!read) return false;
  *write = *read;
  return true;
}

/*! Attempt to copy one container's contents into another.*/
template <class Type1, class Type2>
inline bool try_copy_container(locking_container_base <Type1> &Left,
  locking_container_base <Type2> &Right, null_container_base &Multi,
  lock_auth_base::auth_type &Authorization, bool Block = true) {
  return try_copy_container(Left, Right, Multi, Authorization.get(), Block);
}

#endif //locking_container_hpp
