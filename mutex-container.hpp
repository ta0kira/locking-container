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
 *   - 'r_lock': This lock allows multiple readers, but it never allows writers.
 *     This might be useful if you have a container that will never be written
 *     to but you nevertheless need to retain the same container semantics.
 *
 *   - 'broken_lock': This is mostly a joke; however, you can use it to test
 *     pathological cases. This lock will always fail to lock and unlock.
 *
 * Other notes:
 *
 *   - You must enable C++11 (or higher) when using this header.
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

#include <atomic>
#include <memory>

#include <pthread.h>
#include <assert.h>
#include <unistd.h>


/*! \class lock_auth_base
    \brief Base class for lock authorization classes.
    @see lock_auth
 */

class lock_base;

class lock_auth_base {
public:
  virtual bool lock_allowed(bool Read) const = 0;

  virtual inline ~lock_auth_base() {}

private:
  friend class lock_base;

  virtual bool register_auth(bool Read, bool ReadWait, bool WriteWait) = 0;
  virtual void release_auth(bool Read) = 0;
};


/*! \class mutex_container_base
    \brief Base class for \ref mutex_container.
 */

template <class> class mutex_proxy;

template <class Type>
struct mutex_container_base {
  typedef Type                             type;
  typedef mutex_proxy <type>               proxy;
  typedef mutex_proxy <const type>         const_proxy;
  typedef std::shared_ptr <lock_auth_base> auth_type;

  inline proxy get(bool Block = true) {
    return this->get_auth(NULL, Block);
  }

  inline const_proxy get_const(bool Block = true) const {
    return this->get_auth_const(NULL, Block);
  }

  inline proxy get_auth(auth_type &Authorization, bool Block = true) {
    return this->get_auth(Authorization.get(), Block);
  }

  inline const_proxy get_auth_const(auth_type &Authorization, bool Block = true) const {
    return this->get_auth_const(Authorization.get(), Block);
  }

  virtual proxy       get_auth(lock_auth_base *Authorization, bool Block = true)             = 0;
  virtual const_proxy get_auth_const(lock_auth_base *Authorization, bool Block = true) const = 0;

  virtual auth_type get_new_auth() const {
    return auth_type();
  }

  virtual inline ~mutex_container_base() {}
};


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

class rw_lock;
template <class> class lock_auth;

template <class Type, class Lock = rw_lock>
class mutex_container : public mutex_container_base <Type> {
private:
  typedef lock_auth <Lock> auth_base_type;

public:
  typedef mutex_container_base <Type> base;
  using typename base::type;
  using typename base::proxy;
  using typename base::const_proxy;
  using typename base::auth_type;
  //NOTE: this is needed so that the 'lock_auth_base' variants are pulled in
  using base::get_auth;
  using base::get_auth_const;

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
  explicit __attribute__ ((deprecated)) mutex_container(const mutex_container &Copy) : contained() {
    auto_copy(Copy, contained);
  }

  /*! Generalized version of copy constructor.*/
  explicit __attribute__ ((deprecated)) mutex_container(const base &Copy) : contained() {
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
  mutex_container __attribute__ ((deprecated)) &operator = (const mutex_container &Copy) {
    if (&Copy == this) return *this; //(prevents deadlock when copying self)
    return this->operator = (static_cast <const base&> (Copy));
  }

  /*! Generalized version of \ref mutex_container::operator=.*/
  mutex_container __attribute__ ((deprecated)) &operator = (const base &Copy) {
    proxy self = this->get();
    assert(self);
    if (!auto_copy(Copy, *self)) assert(NULL);
    return *this;
  }

  /*! Object version of \ref mutex_container::operator=.*/
  mutex_container __attribute__ ((deprecated)) &operator = (const Type &Object) {
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
   * \param Authorization Authorization object to prevent deadlocks.
   * \param Block Should the call block for a mutex lock?
   *
   * \return proxy object
   */
  inline proxy get_auth(lock_auth_base *Authorization, bool Block = true) {
    //NOTE: no read/write choice is given here!
    return proxy(&contained, &locks, Authorization, Block);
  }

  /*! Const version of \ref mutex_container::get.*/
  inline const_proxy get_auth_const(lock_auth_base *Authorization, bool Block = true) const {
    return const_proxy(&contained, &locks, Authorization, true, Block);
  }

  //@}

  /*! Get a new authorization object.*/
  virtual auth_type get_new_auth() const {
    return mutex_container::new_auth();
  }

  /*! Get a new authorization object.*/
  static auth_type new_auth() {
    return auth_type(new auth_base_type);
  }

private:
  static inline bool auto_copy(const base &copied, type &copy) {
    typename base::const_proxy object = copied.get();
    if (!object) return false;
    copy = *object;
    return true;
  }

  type         contained;
  mutable Lock locks;
};


class lock_base {
public:
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int lock(lock_auth_base *auth, bool read, bool block) = 0;
  /*! Return < 0 must mean failure. Should return the current number of read locks on success.*/
  virtual int unlock(lock_auth_base *auth, bool read) = 0;

protected:
  static inline bool register_auth(lock_auth_base *auth, bool Read, bool ReadWait, bool WriteWait) {
    return auth? auth->register_auth(Read, ReadWait, WriteWait) : true;
  }

  static inline void release_auth(lock_auth_base *auth, bool Read) {
    if (auth) auth->release_auth(Read);
  }
};


template <class Type>
class mutex_proxy_base {
private:
  class locker;
  typedef std::shared_ptr <locker> lock_type;

public:
  template <class> friend class mutex_proxy_base;
  template <class> friend class mutex_proxy;

  mutex_proxy_base() {}

  mutex_proxy_base(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth, bool new_read, bool block) :
    container_lock(new locker(new_pointer, new_locks, new_auth, new_read, block)) {}

  inline int last_lock_count() const {
    //(mostly provided for debugging)
    return container_lock? container_lock->lock_count : 0;
  }

protected:
  inline void opt_out() {
    container_lock.reset();
  }

  inline Type *pointer() {
    return container_lock? container_lock->pointer : 0;
  }

  inline Type *pointer() const {
    return container_lock? container_lock->pointer : 0;
  }

private:
  class locker {
  public:

    locker() : pointer(NULL), lock_count(), read(true), locks(NULL), auth() {}

    locker(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth, bool new_read, bool block) :
      pointer(new_pointer), lock_count(), read(new_read), locks(new_locks), auth(new_auth) {
      if (!locks || (lock_count = locks->lock(auth, read, block)) < 0) this->opt_out(false);
    }

    int last_lock_count() const {
      //(mostly provided for debugging)
      return lock_count;
    }

    bool read_only() const {
      return read;
    }

    inline ~locker() {
      this->opt_out(true);
    }

    void opt_out(bool unlock) {
      pointer    = NULL;
      lock_count = 0;
      if (unlock && locks) locks->unlock(auth, read);
      auth  = NULL;
      locks = NULL;
    }

    Type *pointer;
    int   lock_count;

  private:
    locker(const locker&);
    locker &operator = (const locker&);

    bool            read;
    lock_base      *locks;
    lock_auth_base *auth;
  };

  lock_type container_lock;
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

  mutex_proxy(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth, bool block) :
    mutex_proxy_base <Type> (new_pointer, new_locks, new_auth, false, block) {}

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
    this->opt_out();
    return *this;
  }

  /*! \brief Check if the reference is valid.
   *
   * \return valid (true) or invalid (false)
   */
  inline operator bool() const {
    return this->pointer();
  }

  /*! \brief Check if the reference is invalid.
   *
   * \return invalid (true) or valid (false)
   */
  inline bool operator ! () const {
    return !this->pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy <const Type> &equal) const {
    return this->pointer() == equal.pointer();
  }

  //@}

  /** @name Trivial Iterator Functions
  *
  */
  //@{

  inline operator       Type*()          { return  this->pointer(); }
  inline operator const Type*() const    { return  this->pointer(); }
  inline       Type &operator *()        { return *this->pointer(); }
  inline const Type &operator *() const  { return *this->pointer(); }
  inline       Type *operator ->()       { return  this->pointer(); }
  inline const Type *operator ->() const { return  this->pointer(); }

  //@}
};


template <class Type>
class mutex_proxy <const Type> : public mutex_proxy_base <const Type> {
private:
  template <class, class> friend class mutex_container;

  mutex_proxy(const Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth, bool read, bool block) :
    mutex_proxy_base <const Type> (new_pointer, new_locks, new_auth, read, block) {}

public:
  mutex_proxy() : mutex_proxy_base <const Type> () {}

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
    this->opt_out();
    return *this;
  }

  /*! \brief Check if the reference is valid.
   *
   * \return valid (true) or invalid (false)
   */
  inline operator bool() const {
    return this->pointer();
  }

  /*! \brief Check if the reference is invalid.
   *
   * \return invalid (true) or valid (false)
   */
  inline bool operator ! () const {
    return !this->pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const mutex_proxy <Type> &equal) const {
    return this->pointer() == equal.pointer();
  }

  //@}

  /** @name Trivial Iterator Functions
  *
  */
  //@{

  inline operator const Type*() const    { return  this->pointer(); }
  inline const Type &operator *() const  { return *this->pointer(); }
  inline const Type *operator ->() const { return  this->pointer(); }

  //@}
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
  int lock(lock_auth_base *auth, bool read, bool block) {
    if (pthread_mutex_lock(&master_lock) != 0) return -1;
    //make sure this is an authorized lock type for the caller
    if (!register_auth(auth, read, readers_waiting, writer_waiting)) {
      pthread_mutex_unlock(&master_lock);
      return -1;
    }
    //check for blocking behavior
    bool must_block = writer || writer_waiting || (!read && readers);
    if (!block && must_block) {
      release_auth(auth, read);
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
          release_auth(auth, read);
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
          release_auth(auth, read);
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
          release_auth(auth, read);
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
  w_lock() {
    pthread_mutex_init(&write_lock, NULL);
  }

private:
  w_lock(const w_lock&);
  w_lock &operator = (const w_lock&);

public:
  int lock(lock_auth_base *auth, bool /*unused*/, bool block) {
    if (!register_auth(auth, false, 0, 0)) return -1;
    if ((block? pthread_mutex_lock : pthread_mutex_trylock)(&write_lock) != 0) {
      release_auth(auth, false);
      return -1;
    }
    return 0;
  }

  int unlock(lock_auth_base *auth, bool /*unused*/) {
    release_auth(auth, false);
    return (pthread_mutex_unlock(&write_lock) == 0)? 0 : -1;
  }

  ~w_lock() {
    pthread_mutex_destroy(&write_lock);
  }

private:
  pthread_mutex_t write_lock;
};


/*! \class r_lock
    \brief Lock object that allows multiple readers but no writers.
 */

class r_lock : public lock_base {
public:
  r_lock() : counter(0) {}

  int lock(lock_auth_base *auth, bool read, bool /*unused*/) {
    if (!read) return -1;
    if (!register_auth(auth, read, 0, 0)) return -1;
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
  int lock(lock_auth_base* /*unused*/, bool /*unused*/, bool /*unused*/) { return -1; }
  int unlock(lock_auth_base* /*unused*/, bool /*unused*/)                { return -1; }
};


/*! \class lock_auth
    \brief Lock authorization object.
    @see mutex_container::auth_type
    @see mutex_container::get_new_auth
    @see mutex_container::get_auth
    @see mutex_container::get_auth_const

    This class is used by \ref mutex_container to prevent deadlocks. To prevent
    deadlocks, create one \ref lock_auth instance per thread, and pass it to
    the \ref mutex_container when getting a proxy object. This will prevent the
    thread from obtaining an new incompatible lock type when it already holds a
    lock.
 */

template <class>
class lock_auth : public lock_auth_base {
public:
  lock_auth() : used(false) {}

  bool lock_allowed(bool /*unused*/) const {
    return !used;
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool /*unused*/, bool /*unused*/, bool /*unused*/) {
    if (used) return false;
    return (used = true);
  }

  void release_auth(bool /*unused*/) {
    assert(used);
    used = false;
  }

  bool used;
};

template <>
class lock_auth <rw_lock> : public lock_auth_base {
public:
  lock_auth() : counter(0), write(false) {}

  bool lock_allowed(bool Read) const {
    if (Read) return !write;
    else      return !write && !counter;
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool Read, bool ReadWait, bool WriteWait) {
    if (write) return false;
    //reject if this thread is blocking writers
    if (counter && (WriteWait || !Read)) return false;
    if (Read) {
      ++counter;
      assert(counter > 0);
    } else {
      if (counter) return false;
      write = true;
    }
    return true;
  }

  void release_auth(bool Read) {
    if (Read) {
      assert(counter && !write);
      --counter;
    } else {
      assert(write && !counter);
      write = false;
    }
  }

  int  counter;
  bool write;
};

template <>
class lock_auth <r_lock> : public lock_auth_base {
public:
  bool lock_allowed(bool Read) const { return Read; }

private:
  bool register_auth(bool Read, bool /*unused*/, bool /*unused*/) { return Read; }
  void release_auth(bool Read) { assert(Read); }
};

template <>
class lock_auth <broken_lock> : public lock_auth_base {
public:
  bool lock_allowed(bool /*unused*/) const { return false; }

private:
  bool register_auth(bool /*unused*/, bool /*unused*/, bool /*unused*/) { return false; }
  void release_auth(bool /*unused*/) { assert(false); }
};

#endif //mutex_container_hpp
