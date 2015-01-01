/* Container that returns a mutex-locking proxy
 * (c) 2014-2015, Kevin P. Barry [ta0kira@gmail.com]
 *
 *
 * This file provides a template class that protects access to an object with a
 * pthread mutex. Conventional mutex-protection methods are susceptible to bugs
 * caused by forgetting (or neglecting) to lock/unlock the mutex. This class
 * eliminates the need to remember, because the only access to the protected
 * object is via a proxy object that keeps the mutex locked until the proxy
 * destructs.
 *
 * This header contains one main class 'mutex_container <class, class>', where
 * the first argument is the type of object being protected, and the second is
 * the type of lock to be used. A few lock choices are provided:
 *
 *   - 'rw_lock': This lock allows multiple readers at a time. This is the
 *     default lock used. A write lock can only be obtained if no other readers
 *     or writers have a lock. If a thread attempts to obtain a write lock and
 *     there are readers, it will block until all readers leave, blocking out
 *     all new readers and writers in the meantime.
 *
 *   - 'w_lock': This lock doesn't make a distinction between readers and
 *     writers; only one thread can hold a lock at any given time. This should
 *     operate faster if you don't need read locks.
 *
 *   - 'broken_lock <bool>': This is mostly a joke; however, you can use it to
 *     test pathological cases. The template parameter indicates whether or not
 *     locks/unlocks will succeed. Note that "lock" is dubious here; it simply
 *     returns success or failure, without locking anything. If you want to see
 *     what happens to your code when a lock can never be obtained, use
 *     'broken_lock <>' as your lock.
 *
 * Other notes:
 *
 *   - You might need to link your executable with libpthread after compiling.
 *
 *   - The assignment operators for 'mutex_container' can cause assertions if
 *     the mutex can't be locked. The assignment operators are too useful to
 *     eliminate, and assertions seem to be the only logical behavior, with the
 *     other choice being silent failure to assign.
 */


/*! \file mutex-container.hpp
 *  \author Kevin P. Barry
 *  \brief Mutex-protected Container Class.
 *
 * The class in this header is used to protect objects with a mutex. The class
 * provides access via a proxy object that locks the mutex upon construction and
 * unlocks it upon the destruction of the last reference (see
 * \ref mutex_container::get.)
 */

#ifndef mutex_container_hpp
#define mutex_container_hpp

#include <pthread.h>
#include <assert.h>
#include <unistd.h>


/*! \class mutex_container
    \brief Container to protect an object with a mutex.

    Each instance of this class contains a mutex and an encapsulated object of
    the type denoted by the template parameter. The \ref mutex_container::get
    and \ref mutex_container::get_const functions provide a proxy object (see
    \ref mutex_proxy) that automatically locks and unlocks the mutex to simplify
    code that accesses the encapsulated object.
    \attention This class contains a mutable member, which means that a memory
    page containing even a const instance should not be remapped as read-only.
    \note This is not a "container" in the STL sense.
 */

template <class> class mutex_proxy;
class rw_lock;

template <class Type, class Lock = rw_lock>
class mutex_container {
public:
  typedef Type type;
  typedef mutex_proxy <type>       proxy;
  typedef mutex_proxy <const type> const_proxy;

  /*! \brief Constructor.
   *
   * \param Object Object to copy as contained object.
   */
  explicit mutex_container(const type &Object = type()) : contained(Object) {}

  /*! \brief Copy constructor.
   *
   * \param Copy Instance to copy.
   * \attention This function will block if the mutex for "Copy" is
   * locked.
   */
  mutex_container(const mutex_container &Copy) : contained() {
    auto_copy(Copy, contained);
  }

  template <class Type2, class Lock2>
  mutex_container(const mutex_container <Type2, Lock2> &Copy) : contained() {
    auto_copy(Copy, contained);
  }

  /*! \brief Assignment operator.
   *
   * \param Copy Instance to copy.
   * \attention This function will block if the mutex for either object is
   * locked. The mutex of "Copy" is locked first, then the mutex for this
   * object. The mutex for "Copy" will remain locked until the locking of
   * the mutex for this object is either succeeds or fails.
   * \attention This will cause an assertion if the mutex can't be locked.
   *
   * \return *this
   */
  mutex_container &operator = (const mutex_container &Copy) {
    if (&Copy == this) return *this; //(prevents deadlock when copying self)
    return this->operator = <> (Copy);
  }

  /*! Template version of \ref mutex_container::operator= .*/
  template <class Type2, class Lock2>
  mutex_container &operator = (const mutex_container <Type2, Lock2> &Copy) {
    proxy self = this->get();
    assert(self);
    if (!auto_copy(Copy, *self)) assert(NULL);
    return *this;
  }

  /*! Object version of \ref mutex_container::operator= .*/
  mutex_container &operator = (const Type &Object) {
    proxy self = this->get();
    assert(self);
    *self = Object;
    return *this;
  }

  /*! \brief Destructor.
   *
   * \attention This will block if the mutex is locked.
   */
  ~mutex_container() {
    proxy self = this->get();
  }

  /** @name Accessor Functions
  *
  */
  //@{

  /*! \brief Retrieve a proxy to the contained object.
   *
   * Retrieve a proxy object that automatically manages mutex locking and
   * unlocking. If "Block" is true then the call blocks until the mutex
   * can be locked. If it's false, the call returns immediately, with the
   * possibility of not obtaining a lock.
   * @see mutex_proxy
   * \attention Always check that the returned object contains a valid
   * reference with mutex_proxy::operator!. The reference will always be
   * invalid if a mutex lock hasn't been obtained.
   * \attention The returned object should only be passed by value, and it
   * should only be passed within the same thread that
   * \ref mutex_container::get was called from. This is because the proxy
   * object uses reference counting that isn't reentrant.
   * \param Block Should the call block for a mutex lock?
   *
   * \return proxy object
   */
  inline proxy get(bool Block = true) {
    //NOTE: no read/write choice is given here!
    return proxy(&contained, &locks, Block);
  }

  /*! Const version of \ref mutex_container::get .*/
  inline const_proxy get(bool Block = true) const {
    return this->get_const(Block);
  }

  /*! Const version of \ref mutex_container::get .*/
  inline const_proxy get_const(bool Block = true) const {
    return const_proxy(&contained, &locks, true, Block);
  }

  //@}

private:
  template <class Type2, class Lock2>
  static inline bool auto_copy(const mutex_container <Type2, Lock2> &copied, type &copy) {
    typename mutex_container <Type2, Lock2> ::const_proxy object = copied.get();
    if (!object) return false;
    copy = *object;
    return true;
  }

  type         contained;
  mutable Lock locks;
};


struct lock_base {
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int lock(bool read, bool block) = 0;
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int unlock(bool read) = 0;
};


template <class Type>
class mutex_proxy_base {
public:
  template <class> friend class mutex_proxy_base;
  template <class> friend class mutex_proxy;

  mutex_proxy_base() : pointer(NULL), read(true), locks(NULL), counter(NULL) {}

  mutex_proxy_base(Type *new_pointer, lock_base *new_locks, bool new_read, bool block) :
    pointer(new_pointer), read(new_read), locks(new_locks), counter(new int(1)), lock_count() {
    if (!locks || (lock_count = locks->lock(read, block)) < 0) this->opt_out(false);
  }

  explicit mutex_proxy_base(const mutex_proxy_base &copy) :
    pointer(NULL), read(true), locks(NULL), counter(NULL), lock_count() {
    *this = copy;
  }

  template <class Type2>
  explicit mutex_proxy_base(const mutex_proxy_base <Type2> &copy) :
    pointer(NULL), read(true), locks(NULL), counter(NULL), lock_count() {
    *this = copy;
  }

  mutex_proxy_base &operator = (const mutex_proxy_base &copy) {
    if (&copy == this) return *this;
    return this->operator = <Type> (copy);
  }

  template <class Type2>
  mutex_proxy_base &operator = (const mutex_proxy_base <Type2> &copy) {
    this->opt_out(true);
    counter = copy.counter;
    if (counter) ++*counter;
    pointer    = counter? copy.pointer    : NULL;
    read       = copy.read;
    locks      = counter? copy.locks      : NULL;
    lock_count = counter? copy.lock_count : 0;
    return *this;
  }

  int last_lock_count() const {
    //(mostly provided for debugging)
    return lock_count;
  }

  bool read_only() const {
    return read;
  }

  inline ~mutex_proxy_base() {
    this->opt_out(true);
  }

protected:
  void opt_out(bool unlock) {
    if (counter && --*counter <= 0) {
      pointer = NULL;
      int *old_counter = counter;
      counter = NULL;
      delete old_counter;
      if (unlock && locks) locks->unlock(read);
    }
    pointer    = NULL;
    locks      = NULL;
    counter    = NULL;
    lock_count = 0;
  }

private:
  Type      *pointer;
  bool       read;
  lock_base *locks;
  int       *counter, lock_count;
};


/*! \class mutex_proxy
    \brief Proxy object for \ref mutex_container access.

    Instances of this class are returned by \ref mutex_container instances as
    proxy objects that access the contained object. The mutex of the
    \ref mutex_container is locked upon return of this object and references to
    the returned object are counted as it's copied. Upon destruction of the last
    reference the mutex is unlocked.
 */

template <class Type>
class mutex_proxy : public mutex_proxy_base <Type> {
private:
  template <class, class> friend class mutex_container;
  template <class>        friend class mutex_proxy;

  mutex_proxy(Type *new_pointer, lock_base *new_locks, bool block) :
    mutex_proxy_base <Type> (new_pointer, new_locks, false, block) {}

public:
  mutex_proxy() : mutex_proxy_base <Type> () {}

  /** @name Checking Referred-to Object
  *
  */
  //@{

  /*! \brief Clear the reference and unlock the mutex.
   *
   * The mutex isn't unlocked until the last reference is destructed. This
   * will clear the reference for this object alone and decrement the
   * reference count by one. If the new reference count is zero then the
   * mutex is unlocked.
   *
   * \return *this
   */
  inline mutex_proxy &clear() {
    this->opt_out(true);
    return *this;
  }

  /*! \brief Check if the reference is valid.
   *
   * \return valid (true) or invalid (false)
   */
  inline operator bool() const {
    return mutex_proxy_base <Type> ::pointer;
  }

  /*! \brief Check if the reference is invalid.
   *
   * \return invalid (true) or valid (false)
   */
  inline bool operator ! () const {
    return !mutex_proxy_base <Type> ::pointer;
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy &equal) const {
    return mutex_proxy_base <Type> ::pointer == equal.mutex_proxy_base <Type> ::pointer;
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy <const Type> &equal) const {
    return mutex_proxy_base <Type> ::pointer == equal.mutex_proxy_base <const Type> ::pointer;
  }

  //@}

  /** @name Trivial Iterator Functions
  *
  */
  //@{

  inline operator       Type*()          { return mutex_proxy_base <Type> ::pointer; }
  inline operator const Type*() const    { return mutex_proxy_base <Type> ::pointer; }
  inline       Type &operator *()        { return *mutex_proxy_base <Type> ::pointer; }
  inline const Type &operator *() const  { return *mutex_proxy_base <Type> ::pointer; }
  inline       Type *operator ->()       { return mutex_proxy_base <Type> ::pointer; }
  inline const Type *operator ->() const { return mutex_proxy_base <Type> ::pointer; }

  //@}
};


template <class Type>
class mutex_proxy <const Type> : public mutex_proxy_base <const Type> {
private:
  template <class, class> friend class mutex_container;

  mutex_proxy(const Type *new_pointer, lock_base *new_locks, bool read, bool block) :
    mutex_proxy_base <const Type> (new_pointer, new_locks, read, block) {}

public:
  mutex_proxy() : mutex_proxy_base <const Type> () {}

  mutex_proxy(const mutex_proxy <Type> &Copy) :
    mutex_proxy_base <const Type> (Copy) {}

  /** @name Checking Referred-to Object
  *
  */
  //@{

  /*! \brief Clear the reference and unlock the mutex.
   *
   * The mutex isn't unlocked until the last reference is destructed. This
   * will clear the reference for this object alone and decrement the
   * reference count by one. If the new reference count is zero then the
   * mutex is unlocked.
   *
   * \return *this
   */
  inline mutex_proxy &clear() {
    this->opt_out(true);
    return *this;
  }

  /*! \brief Copy from a non-const instance.
   *
   * Assign an instance of this class of the same type, but non-const.
   * \param Copy An object to copy.
   *
   * \return *this
   */
  mutex_proxy &operator = (const mutex_proxy <Type> &Copy) {
    mutex_proxy_base <const Type> ::operator = (Copy);
    return *this;
  }

  /*! \brief Check if the reference is valid.
   *
   * \return valid (true) or invalid (false)
   */
  inline operator bool() const {
    return mutex_proxy_base <const Type> ::pointer;
  }

  /*! \brief Check if the reference is invalid.
   *
   * \return invalid (true) or valid (false)
   */
  inline bool operator ! () const {
    return !mutex_proxy_base <const Type> ::pointer;
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy &equal) const {
    return mutex_proxy_base <const Type> ::pointer == equal.mutex_proxy_base <const Type> ::pointer;
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy <Type> &equal) const {
    return mutex_proxy_base <const Type> ::pointer == equal.mutex_proxy_base <Type> ::pointer;
  }

  //@}

  /** @name Trivial Iterator Functions
  *
  */
  //@{

  inline operator const Type*() const    { return mutex_proxy_base <const Type> ::pointer; }
  inline const Type &operator *() const  { return *mutex_proxy_base <const Type> ::pointer; }
  inline const Type *operator ->() const { return mutex_proxy_base <const Type> ::pointer; }

  //@}
};


/*! \class broken_lock
    \brief Mutex object that is permanently broken. (Not really useful.)
 */

template <bool State = false>
struct broken_lock : public lock_base {
  int lock(bool /*unused*/, bool /*unused*/) { return State? 0 : -1; }
  int unlock(bool /*unused*/)                { return State? 0 : -1; }
};


/*! \class rw_lock
    \brief Mutex object used internally by mutex_container.
 */

class rw_lock : public lock_base {
public:
  rw_lock() : counter(0), write_waiting(false), counter_lock(), write_lock(), write_wait() {}

private:
  rw_lock(const rw_lock&);
  rw_lock &operator = (const rw_lock&);

public:
  int lock(bool read, bool block) {
    //lock the write lock so that a waiting write thread blocks new reads
    if ((block? pthread_mutex_lock : pthread_mutex_trylock)(&write_lock) != 0) return -1;
    //lock the counter lock to check or increment the counter
    //NOTE: this should only ever block for a trivial amount of time
    if (pthread_mutex_lock(&counter_lock)  != 0) {
      pthread_mutex_unlock(&write_lock);
      return -1;
    }
    if (read) {
      int new_counter = ++counter;
      //if for some strange reason there's an overflow...
      assert(counter > 0);
      pthread_mutex_unlock(&counter_lock);
      pthread_mutex_unlock(&write_lock);
      return new_counter;
    } else {
      if (block && counter) {
        //only wait if there are currently readers and we're to block
        assert(!write_waiting);
        write_waiting = true;
        //NOTE: 'counter_lock' must be locked here, but if the wait blocks then it gets unlocked
        //(no need to check return; if it fails, 'counter' will still be non-zero because 'counter_lock' won't have unlocked)
        pthread_cond_wait(&write_wait, &counter_lock);
        write_waiting = false;
      }
      //NOTE: must check 'counter' before unlocking 'counter_lock'!
      if (counter) {
        pthread_mutex_unlock(&counter_lock);
        //failed to obtain a lock, either due to not blocking or a wait error
        pthread_mutex_unlock(&write_lock);
        return -1;
      } else {
        //'write_lock' is retained until 'unlock' is called so that no new locks can happen
        pthread_mutex_unlock(&counter_lock);
        return 0;
      }
    }
  }

  int unlock(bool read) {
    //NOTE: this must be called exactly once per lock instance!
    if (read) {
      //this should only ever be locked for a short period of time
      if (pthread_mutex_lock(&counter_lock) != 0) return -1;
      assert(counter > 0);
      int new_counter = --counter;
      if (counter == 0 && write_waiting) pthread_cond_broadcast(&write_wait);
      pthread_mutex_unlock(&counter_lock);
      return new_counter;
    } else {
      //anything waiting for a lock should currently be blocking on 'write_lock'
      return pthread_mutex_unlock(&write_lock);
    }
  }

  ~rw_lock() {
    pthread_mutex_destroy(&counter_lock);
    pthread_mutex_destroy(&write_lock);
    pthread_cond_destroy(&write_wait);
  }

private:
  int             counter;
  bool            write_waiting;
  pthread_mutex_t counter_lock, write_lock;
  pthread_cond_t  write_wait;
};


/*! \class w_lock
    \brief Mutex object used internally by mutex_container.
 */

class w_lock : public lock_base {
public:
  w_lock() : write_lock() {}

private:
  w_lock(const w_lock&);
  w_lock &operator = (const w_lock&);

public:
  int lock(bool /*unused*/, bool block) {
    return ((block? pthread_mutex_lock : pthread_mutex_trylock)(&write_lock) == 0)? 0 : -1;
  }

  int unlock(bool /*unused*/) {
    return (pthread_mutex_unlock(&write_lock) == 0)? 0 : -1;
  }

  ~w_lock() {
    pthread_mutex_destroy(&write_lock);
  }

private:
  pthread_mutex_t write_lock;
};


#endif //mutex_container_hpp
