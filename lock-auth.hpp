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

#ifndef authorization_hpp
#define authorization_hpp

#include <assert.h>


/*! \class lock_auth_base
    \brief Base class for lock authorization classes.
    @see lock_auth
 */

class lock_base;

class lock_auth_base {
public:
  typedef long count_type;
  typedef std::shared_ptr <lock_auth_base> auth_type;

  virtual count_type reading_count() const { return 0; }
  virtual count_type writing_count() const { return 0; }
  virtual bool       always_read()   const { return false; }
  virtual bool       always_write()  const { return false; }

  /*! Attempt to predict if a read authorization would be granted.*/
  inline bool guess_read_allowed(bool lock_out = true, bool in_use = true) const {
    return this->test_auth(true, lock_out, in_use);
  }

  /*! Attempt to predict if a write authorization would be granted.*/
  inline bool guess_write_allowed(bool lock_out = true, bool in_use = true) const {
    return this->test_auth(false, lock_out, in_use);
  }

  virtual inline ~lock_auth_base() {}

private:
  friend class lock_base;

  /*! Obtain lock authorization.*/
  virtual bool register_auth(bool read, bool lock_out, bool in_use) = 0;

  /*! Test lock authorization.*/
  virtual bool test_auth(bool read, bool lock_out, bool in_use) const = 0;

  /*! Release lock authorization.*/
  virtual void release_auth(bool read) = 0;
};


/*! \class lock_auth
    \brief Lock authorization object.
    @see locking_container::auth_type
    @see locking_container::get_new_auth
    @see locking_container::get_auth
    @see locking_container::get_auth_const

    This class is used by \ref locking_container to prevent deadlocks. To
    prevent deadlocks, create one \ref lock_auth instance per thread, and pass
    it to the \ref locking_container when getting a proxy object. This will
    prevent the thread from obtaining an new incompatible lock type when it
    already holds a lock.
 */

template <class> class lock_auth;

/*! \class lock_auth <rw_lock>
 *
 * This auth. type allows the caller to hold multiple read locks, or a single
 * write lock, but not both. Note that if another thread is waiting for a write
 * lock on the container and the caller already has a read lock then the lock
 * will be rejected. Two exceptions to these rules are: 1) if the container to
 * be locked currently has no other locks; 2) if the call isn't blocking and
 * it's for a write lock.
 */

class rw_lock;

template <>
class lock_auth <rw_lock> : public lock_auth_base {
public:
  using lock_auth_base::count_type;

  lock_auth() : reading(0), writing(0) {}

  count_type reading_count() const { return reading; }
  count_type writing_count() const { return writing; }

  ~lock_auth() {
    //NOTE: this can't be in '~lock_auth_base'!
    assert(!this->reading_count() && !this->writing_count());
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool read, bool lock_out, bool in_use) {
    if (!this->test_auth(read, lock_out, in_use)) return false;
    if (read) {
      ++reading;
      assert(reading > 0);
    } else {
      ++writing;
      assert(writing > 0);
    }
    return true;
  }

  bool test_auth(bool read, bool lock_out, bool in_use) const {
    if (writing && in_use)                return false;
    if (reading && !read && in_use)       return false;
    if ((reading || writing) && lock_out) return false;
    return true;
  }

  void release_auth(bool read) {
    if (read) {
      //NOTE: don't check 'writing' because there are a few exceptions!
      assert(reading > 0);
      --reading;
    } else {
      //NOTE: don't check 'reading' because there are a few exceptions!
      assert(writing > 0);
      --writing;
    }
  }

  count_type reading, writing;
};

/*! \class lock_auth <r_lock>
 *
 * This auth. type allows the caller to hold multiple read locks, but no write
 * locks. Note that if another thread is waiting for a write lock on the
 * container and the caller already has a read lock then the lock will be
 * rejected. Use this auth. type if you want to ensure that a thread doesn't
 * obtain a write lock on any container.
 */

class r_lock;

template <>
class lock_auth <r_lock> : public lock_auth_base {
public:
  using lock_auth_base::count_type;

  lock_auth() : reading(0) {}

  count_type reading_count() const { return reading; }
  bool       always_read()   const { return true; }

  ~lock_auth() {
    //NOTE: this can't be in '~lock_auth_base'!
    //NOTE: no point checking 'writing_count', since it's overrides will be ignored here
    assert(!this->reading_count());
  }

private:
  bool register_auth(bool read, bool lock_out, bool in_use) {
    if (!this->test_auth(read, lock_out, in_use)) return false;
    ++reading;
    assert(reading > 0);
    return true;
  }

  bool test_auth(bool read, bool lock_out, bool /*in_use*/) const {
    if (!read)               return false;
    if (reading && lock_out) return false;
    return true;
  }

  void release_auth(bool read) {
    assert(read);
    assert(reading > 0);
    --reading;
  }

  count_type reading;
};

/*! \class lock_auth <w_lock>
 *
 * This auth. type allows the caller to hold no more than one lock at a time,
 * regardless of lock type. An exception to this behavior is if the container to
 * be locked currently has no other locks. Use this auth. type if you are only
 * using containers that use 'w_lock', or if you want to disallow multiple read
 * locks on containers that use 'rw_lock' or 'r_lock'.
 */

class w_lock;

template <>
class lock_auth <w_lock> : public lock_auth_base {
public:
  using lock_auth_base::count_type;

  lock_auth() : writing(0) {}

  count_type writing_count() const { return writing; }
  bool       always_write()  const { return true; }

  ~lock_auth() {
    //NOTE: this can't be in '~lock_auth_base'!
    //NOTE: no point checking 'reading_count', since it's overrides will be ignored here
    assert(!this->writing_count());
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool read, bool lock_out, bool in_use) {
    if (!this->test_auth(read, lock_out, in_use)) return false;
    ++writing;
    assert(writing > 0);
    return true;
  }

  bool test_auth(bool /*read*/, bool /*lock_out*/, bool in_use) const {
    return !writing || !in_use;
  }

  void release_auth(bool /*read*/) {
    assert(writing > 0);
    --writing;
  }

  count_type writing;
};

/*! \class lock_auth <broken_lock>
 *
 * This auth. type doesn't allow the caller to obtain any locks.
 */

struct broken_lock;

template <>
class lock_auth <broken_lock> : public lock_auth_base {
public:
  using lock_auth_base::count_type;

private:
  bool register_auth(bool /*read*/, bool /*lock_out*/, bool /*in_use*/)   { return false; }
  bool test_auth(bool /*read*/, bool /*lock_out*/, bool /*in_use*/) const { return false; }
  void release_auth(bool /*read*/)                                        { assert(false); }
};

#endif //authorization_hpp
