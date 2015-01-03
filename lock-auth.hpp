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

#include "locks.hpp"


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

//NOTE: 'lock_auth_base' is defined in locks.hpp

template <class>
class lock_auth : public lock_auth_base {
public:
  lock_auth() : writing(0) {}

  virtual int  writing_count() const { return writing; }
  virtual bool always_write()  const { return true; }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool /*Read*/, bool /*LockOut*/, bool InUse, bool TestAuth) {
    if (writing && InUse) return false;
    if (TestAuth) return true;
    ++writing;
    assert(writing > 0);
    return true;
  }

  void release_auth(bool /*Read*/) {
    assert(writing > 0);
    --writing;
  }

  int writing;
};

template <>
class lock_auth <rw_lock> : public lock_auth_base {
public:
  lock_auth() : reading(0), writing(0) {}

  virtual int reading_count() const { return reading; }
  virtual int writing_count() const { return writing; }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool Read, bool LockOut, bool InUse, bool TestAuth) {
    if (writing && InUse)                return false;
    if (reading && !Read && InUse)       return false;
    if ((reading || writing) && LockOut) return false;
    if (TestAuth) return true;
    if (Read) {
      ++reading;
      assert(reading > 0);
    } else {
      ++writing;
      assert(writing > 0);
    }
    return true;
  }

  void release_auth(bool Read) {
    if (Read) {
      //NOTE: don't check 'writing' because there are a few exceptions!
      assert(reading > 0);
      --reading;
    } else {
      //NOTE: don't check 'reading' because there are a few exceptions!
      assert(writing > 0);
      --writing;
    }
  }

  int  reading, writing;
};

template <>
class lock_auth <r_lock> : public lock_auth_base {
public:
  lock_auth() : reading(0) {}

  virtual int  reading_count() const { return reading; }
  virtual bool always_read()   const { return true; }

private:
  bool register_auth(bool Read, bool LockOut, bool /*InUse*/, bool TestAuth) {
    if (!Read)              return false;
    if (reading && LockOut) return false;
    if (TestAuth) return true;
    ++reading;
    assert(reading > 0);
    return true;
  }

  void release_auth(bool Read) {
    assert(Read);
    assert(reading > 0);
    --reading;
  }

  int  reading;
};

template <>
class lock_auth <broken_lock> : public lock_auth_base {
private:
  bool register_auth(bool /*Read*/, bool /*LockOut*/, bool /*InUse*/, bool /*TestAuth*/) { return false; }
  void release_auth(bool /*Read*/) { assert(false); }
};

#endif //authorization_hpp
