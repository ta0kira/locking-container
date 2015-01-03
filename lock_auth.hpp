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

  bool lock_allowed(bool /*Read*/, bool /*Block*/ = true) const {
    return !writing;
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool /*Read*/, bool /*Block*/, bool /*LockOut*/, bool InUse, bool TestAuth) {
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

  bool lock_allowed(bool Read, bool Block = true) const {
    if (!Block && !Read) return true;
    if (Read) return !writing;
    else      return !writing && !reading;
  }

private:
  lock_auth(const lock_auth&);
  lock_auth &operator = (const lock_auth&);

  bool register_auth(bool Read, bool Block, bool LockOut, bool InUse, bool TestAuth) {
    if (!Block && !Read)                 return true;
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

  bool lock_allowed(bool Read, bool /*Block*/ = true) const { return Read; }

private:
  bool register_auth(bool Read, bool /*Block*/, bool LockOut, bool /*InUse*/, bool TestAuth) {
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
public:
  bool lock_allowed(bool /*Read*/, bool /*Block*/ = true) const { return false; }

private:
  bool register_auth(bool /*Read*/, bool /*Block*/, bool /*LockOut*/, bool /*InUse*/, bool /*TestAuth*/) { return false; }
  void release_auth(bool /*Read*/) { assert(false); }
};

#endif //authorization_hpp
