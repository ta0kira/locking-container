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

#ifndef lc_authorization_hpp
#define lc_authorization_hpp

#include <set>

#include <assert.h>

namespace lc {


/*! \class lock_auth_base
 *  \brief Base class for lock authorization classes.
 *
 * @see lock_auth
 */

class lock_base;

class lock_auth_base {
public:
  typedef long          count_type;
  typedef unsigned long order_type;
  typedef std::shared_ptr <lock_auth_base> auth_type;

  virtual count_type reading_count() const;
  virtual count_type writing_count() const;

  /*! Attempt to predict if a read authorization would be granted.*/
  inline bool guess_read_allowed(bool lock_out = true, bool in_use = true,
    order_type order = order_type()) const {
    return this->test_auth(true, lock_out, in_use, order);
  }

  /*! Attempt to predict if a write authorization would be granted.*/
  inline bool guess_write_allowed(bool lock_out = true, bool in_use = true,
    order_type order = order_type()) const {
    return this->test_auth(false, lock_out, in_use, order);
  }

  virtual inline ~lock_auth_base() {}

protected:
  friend class lock_base;

  /*! Obtain lock authorization.*/
  virtual bool register_auth(bool read, bool lock_out, bool in_use, order_type order) = 0;

  /*! Test lock authorization.*/
  virtual bool test_auth(bool read, bool lock_out, bool in_use, order_type order) const = 0;

  /*! Release lock authorization.*/
  virtual void release_auth(bool read, order_type order) = 0;

  /*! Allow locking of a lock with a particular order.*/
  virtual bool order_allowed(order_type order) const;
};


/*! \class lock_auth
 *  \brief Lock authorization object.
 *
 * @see locking_container::auth_type
 * @see locking_container::get_new_auth
 * @see locking_container::get_auth
 * @see locking_container::get_auth_const
 *
 * This class is used by \ref locking_container to prevent deadlocks. To prevent
 * deadlocks, create one \ref lock_auth instance per thread, and pass it to the
 * \ref locking_container when getting a proxy object. This will prevent the
 * thread from obtaining an new incompatible lock type when it already holds a
 * lock.
 */

template <class> class lock_auth;


/*! \class lock_auth_rw_lock
 *
 * This auth. type allows the caller to hold multiple read locks, or a single
 * write lock, but not both. Note that if another thread is waiting for a write
 * lock on a w_lock or rw_lock container and the caller already has a read lock
 * then the lock will be rejected. Three exceptions to these rules are: 1) if
 * the container to be locked currently has no other locks; 2) if the call isn't
 * blocking and it's for a write lock; 3) if the container uses a rw_lock, the
 * caller has a write lock on the container, and the caller is requesting a read
 * lock. (The 3rd case is what allows multi-lock to work.)
 */

class lock_auth_rw_lock : public lock_auth_base {
public:
  using lock_auth_base::count_type;
  using lock_auth_base::order_type;

  lock_auth_rw_lock();

  count_type reading_count() const;
  count_type writing_count() const;

  ~lock_auth_rw_lock();

private:
  lock_auth_rw_lock(const lock_auth_rw_lock&);
  lock_auth_rw_lock &operator = (const lock_auth_rw_lock&);

protected:
  bool register_auth(bool read, bool lock_out, bool in_use, order_type order);
  bool test_auth(bool read, bool lock_out, bool in_use, order_type order) const;
  void release_auth(bool read, order_type order);

private:
  count_type reading, writing;
};

class rw_lock;

template <>
class lock_auth <rw_lock> : public lock_auth_rw_lock {};


/*! \class lock_auth_r_lock
 *
 * This auth. type allows the caller to hold multiple read locks, but no write
 * locks. Note that if another thread is waiting for a write lock on the
 * container and the caller already has a read lock then the lock will be
 * rejected. Use this auth. type if you want to ensure that a thread doesn't
 * obtain a write lock on any container. Using this auth. type with containers
 * that use any combination of r_lock and rw_lock should result in the same
 * behavior as using it with only r_lock containers.
 */

class lock_auth_r_lock : public lock_auth_base {
public:
  using lock_auth_base::count_type;
  using lock_auth_base::order_type;

  lock_auth_r_lock();

  count_type reading_count() const;

  ~lock_auth_r_lock();

private:
  lock_auth_r_lock(const lock_auth_r_lock&);
  lock_auth_r_lock &operator = (const lock_auth_r_lock&);

protected:
  bool register_auth(bool read, bool lock_out, bool in_use, order_type order);
  bool test_auth(bool read, bool lock_out, bool in_use, order_type order) const;
  void release_auth(bool read, order_type order);

private:
  count_type reading;
};

class r_lock;

template <>
class lock_auth <r_lock> : public lock_auth_r_lock {};


/*! \class lock_auth_w_lock
 *
 * This auth. type allows the caller to hold no more than one lock at a time,
 * regardless of lock type. An exception to this behavior is if the container to
 * be locked currently has no other locks. Use this auth. type if you are only
 * using containers that use w_lock, or if you want to disallow multiple read
 * locks on containers that use rw_lock or r_lock. Using this auth. type with
 * containers that use any combination of w_lock and rw_lock should result in
 * the same behavior as using it with only w_lock containers.
 */

class lock_auth_w_lock : public lock_auth_base {
public:
  using lock_auth_base::count_type;
  using lock_auth_base::order_type;

  lock_auth_w_lock();

  count_type writing_count() const;

  ~lock_auth_w_lock();

private:
  lock_auth_w_lock(const lock_auth_w_lock&);
  lock_auth_w_lock &operator = (const lock_auth_w_lock&);

protected:
  bool register_auth(bool read, bool lock_out, bool in_use, order_type order);
  bool test_auth(bool read, bool lock_out, bool in_use, order_type order) const;
  void release_auth(bool read, order_type order);

private:
  count_type writing;
};

class w_lock;

template <>
class lock_auth <w_lock> : public lock_auth_w_lock {};


/*! \class lock_auth_dumb_lock
 *
 * This auth. type only allows the caller to hold one lock at a time. Unlike
 * lock_auth_w_lock, it doesn't matter if the container is in use. (One caveat
 * to this is that r_lock doesn't actually lock; therefore, this auth. can
 * potentially hold multiple locks on r_lock containers at one time.) This is
 * useful if you want to ensure that the caller can only hold a single lock at
 * any given time. This auth. type will not work with multi-locking.
 */

class lock_auth_dumb_lock : public lock_auth_base {
public:
  using lock_auth_base::count_type;
  using lock_auth_base::order_type;

  lock_auth_dumb_lock();

  count_type writing_count() const;

  ~lock_auth_dumb_lock();

private:
  lock_auth_dumb_lock(const lock_auth_dumb_lock&);
  lock_auth_dumb_lock &operator = (const lock_auth_dumb_lock&);

protected:
  bool register_auth(bool read, bool lock_out, bool in_use, order_type order);
  bool test_auth(bool read, bool lock_out, bool in_use, order_type order) const;
  void release_auth(bool read, order_type order);

private:
  bool writing;
};

class dumb_lock;

template <>
class lock_auth <dumb_lock> : public lock_auth_dumb_lock {};


/*! \class lock_auth_ordered_lock
 *
 * This auth. type is the same as lock_auth <Type> (first template parameter),
 * except it keeps track of lock orders. If it's used with unordered locks
 * (e.g., rw_lock, etc.), it behaves as lock_auth <Type>. If it's used with
 * ordered locks (e.g., ordered_lock), it will enforce a strict locking order:
 * It will disallow locking a lock with an order <= the highest lock order this
 * auth. holds a lock for. This allows for more lenient deadlock prevention. If
 * the container to be locked isn't currently locked, strict locking order isn't
 * enforced. If at any time this auth. obtains a lock on an unordered container,
 * deadlock prevention reverts to that of lock_auth_rw_lock.
 */

template <class Type>
class lock_auth_ordered_lock : public lock_auth <Type> {
private:
  typedef lock_auth <Type> base;

public:
  using typename base::count_type;
  using typename base::order_type;

  lock_auth_ordered_lock() : unordered_locks(0) {}

  ~lock_auth_ordered_lock() {
    assert(!unordered_locks && !ordered_locks.size());
  }

private:
  lock_auth_ordered_lock(const lock_auth_ordered_lock&);
  lock_auth_ordered_lock &operator = (const lock_auth_ordered_lock&);

protected:
  typedef std::set <order_type> order_set;

  bool order_allowed(order_type order) const {
    return true;
  }

  virtual void register_order(order_type order) {
    if (!order) {
      ++unordered_locks;
      assert(unordered_locks);
    } else {
      assert(ordered_locks.find(order) == ordered_locks.end());
      ordered_locks.insert(order);
    }
  }

  virtual void release_order(order_type order) {
    if (!order) {
      assert(unordered_locks);
      --unordered_locks;
    } else {
      typename order_set::iterator found = ordered_locks.find(order);
      assert(found != ordered_locks.end());
      ordered_locks.erase(found);
    }
  }

  bool register_auth(bool read, bool lock_out, bool in_use, order_type order) {
    //NOTE: this calls the overridden 'order_allowed' above and 'test_auth' below
    if (!this->base::register_auth(read, lock_out, in_use, order)) return false;
    this->register_order(order);
    return true;
  }

  bool test_auth(bool read, bool lock_out, bool in_use, order_type order) const {
    bool normal_rules = !order || unordered_locks;
    //disallow a lock only if it's ordered, that order isn't strictly greater,
    //and the container is currently in use
    if (order && in_use && ordered_locks.size() && *ordered_locks.rbegin() >= order) return false;
    //(if order rules are respected, 'lock_out' and 'in_use' aren't needed)
    return this->base::test_auth(read, normal_rules && lock_out,
      normal_rules && in_use, order);
  }

  void release_auth(bool read, order_type order) {
    this->release_order(order);
    this->base::release_auth(read, order);
  }

private:
  order_set  ordered_locks;
  count_type unordered_locks;
};

template <class> class ordered_lock;

template <>
class lock_auth <ordered_lock <rw_lock> > : public lock_auth_ordered_lock <rw_lock> {};

template <>
class lock_auth <ordered_lock <r_lock> > : public lock_auth_ordered_lock <r_lock> {};

template <>
class lock_auth <ordered_lock <w_lock> > : public lock_auth_ordered_lock <w_lock> {};

template <>
class lock_auth <ordered_lock <dumb_lock> > : public lock_auth_ordered_lock <dumb_lock> {};


/*! \class lock_auth_broken_lock
 *
 * This auth. type doesn't allow the caller to obtain any locks.
 */

class lock_auth_broken_lock : public lock_auth_base {
public:
  using lock_auth_base::count_type;
  using lock_auth_base::order_type;

protected:
  bool register_auth(bool read, bool lock_out, bool in_use, order_type order);
  bool test_auth(bool read, bool lock_out, bool in_use, order_type order) const;
  void release_auth(bool read, order_type order);
};

struct broken_lock;

template <>
class lock_auth <broken_lock> : public lock_auth_broken_lock {};

} //namespace lc

#endif //lc_authorization_hpp
