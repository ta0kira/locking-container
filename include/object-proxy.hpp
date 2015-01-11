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

#ifndef lc_object_proxy_hpp
#define lc_object_proxy_hpp

#include <memory>

#include "locks.hpp"

namespace lc {


template <class Type>
class object_proxy_base {
private:
  class locker;
  typedef std::shared_ptr <locker> lock_type;

public:
  object_proxy_base() {}

  object_proxy_base(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
    bool new_read, bool block, lock_base *new_multi) :
    container_lock(new locker(new_pointer, new_locks, new_auth, new_read, block, new_multi)) {}

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
    locker() : pointer(NULL), lock_count(), read(true), locks(NULL), multi(NULL), auth() {}

    locker(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
      bool new_read, bool block, lock_base *new_multi) :
      pointer(new_pointer), lock_count(), read(new_read), locks(new_locks), multi(new_multi), auth(new_auth) {
      //attempt to lock the multi-lock if there is one (not counted toward 'auth')
      if (multi && multi->lock(auth, true, block, true) < 0) this->opt_out(false, false);
      //attempt to lock the container's lock
      if (!locks || (lock_count = locks->lock(auth, read, block)) < 0) this->opt_out(false);
    }

    int last_lock_count() const {
      //(mostly provided for debugging)
      return lock_count;
    }

    bool read_only() const {
      return read;
    }

    void opt_out(bool unlock1, bool unlock2 = true) {
      pointer    = NULL;
      lock_count = 0;
      if (unlock1 && locks) locks->unlock(auth, read);
      if (unlock2 && multi) multi->unlock(auth, true, true);
      auth  = NULL;
      locks = NULL;
      multi = NULL;
    }

    inline ~locker() {
      this->opt_out(true);
    }

    Type *pointer;
    int   lock_count;

  private:
    locker(const locker&);
    locker &operator = (const locker&);

    bool             read;
    lock_base       *locks, *multi;
    lock_auth_base  *auth;
  };

  lock_type container_lock;
};


/*! \class object_proxy
 *  \brief Proxy object for \ref locking_container access.
 *
 * Instances of this class are returned by \ref locking_container instances as
 * proxy objects that access the contained object. \ref locking_container is
 * locked upon return of this object and references to the returned object are
 * counted as it's copied. Upon destruction of the last reference the container
 * is unlocked.
 */

template <class Type>
class object_proxy : public object_proxy_base <Type> {
private:
  template <class, class> friend class locking_container;

  object_proxy(Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
    bool read, bool block, lock_base *new_multi) :
    object_proxy_base <Type> (new_pointer, new_locks, new_auth, read, block, new_multi) {}

public:
  object_proxy() : object_proxy_base <Type> () {}

  /** @name Checking Referred-to Object
   *
   */
  //@{

  /*! \brief Clear the reference and unlock the container.
   *
   * The container isn't unlocked until the last reference is destructed. This
   * will clear the reference for this object alone and decrement the
   * reference count by one. If the new reference count is zero then the
   * container is unlocked.
   *
   * \return *this
   */
  inline object_proxy &clear() {
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
  inline bool operator == (const object_proxy &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const object_proxy <const Type> &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator != (const object_proxy &equal) const {
    return this->pointer() != equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator != (const object_proxy <const Type> &equal) const {
    return this->pointer() != equal.pointer();
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
class object_proxy <const Type> : public object_proxy_base <const Type> {
private:
  template <class, class> friend class locking_container;

  object_proxy(const Type *new_pointer, lock_base *new_locks, lock_auth_base *new_auth,
    bool read, bool block, lock_base *new_multi) :
    object_proxy_base <const Type> (new_pointer, new_locks, new_auth, read, block, new_multi) {}

public:
  object_proxy() : object_proxy_base <const Type> () {}

  /** @name Checking Referred-to Object
   *
   */
  //@{

  /*! \brief Clear the reference and unlock the container.
   *
   * The container isn't unlocked until the last reference is destructed. This
   * will clear the reference for this object alone and decrement the
   * reference count by one. If the new reference count is zero then the
   * container is unlocked.
   *
   * \return *this
   */
  inline object_proxy &clear() {
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
  inline bool operator == (const object_proxy &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator == (const object_proxy <Type> &equal) const {
    return this->pointer() == equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator != (const object_proxy &equal) const {
    return this->pointer() != equal.pointer();
  }

  /*! \brief Compare the two referenced objects.
   *
   * \return equal (true) or unequal (false)
   */
  inline bool operator != (const object_proxy <Type> &equal) const {
    return this->pointer() != equal.pointer();
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


class null_container;

template <>
class object_proxy <void> : public object_proxy_base <void> {
private:
  friend class multi_lock;

  object_proxy(bool value, lock_base *new_locks, lock_auth_base *new_auth,
    bool read, bool block, lock_base *new_multi) :
    object_proxy_base <void> ((void*) value, new_locks, new_auth, read, block, new_multi) {}

public:
  object_proxy() : object_proxy_base <void> () {}

  inline object_proxy &clear() {
    this->opt_out();
    return *this;
  }

  inline operator bool() const {
    return this->pointer();
  }

  inline bool operator ! () const {
    return !this->pointer();
  }
};

} //namespace lc

#endif //lc_object_proxy_hpp
