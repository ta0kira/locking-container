/* This is a very simple example of the semantics of 'locking_container'. For a
 * non-trivial example, see test.cpp.
 *
 * Compile this program enabling C++11, and linking with libpthread if needed.
 * When you run this program, you should see no output or errors. An assertion
 * means a bug in the code.
 *
 * Suggested compilation command:
 *   c++ -Wall -pedantic -std=c++11 -I../include simple.cpp -o simple -lpthread
 */

#include <stdio.h>
#include <assert.h>

#include "locking-container.hpp"
//(necessary for non-template source)
#include "locking-container.inc"


int main() {
  //default: use 'rw_lock'
  typedef lc::locking_container <int> protected_int0;

  //use 'w_lock' instead
  typedef lc::locking_container <int, lc::w_lock> protected_int1;

  //the two types above share the same base class because they both protect 'int'
  typedef protected_int0::base base;

  //protected data
  protected_int0 data0;
  protected_int1 data1;

  //authorization object to prevent deadlocks (one per thread)
  //NOTE: this will correspond to 'rw_lock', since that's what 'protected_int0' uses
  lc::lock_auth_base::auth_type auth(protected_int0::new_auth());
  //make sure an authorization was provided
  assert(auth);

  //alternatively, you can explicitly specify an authorization type
  lc::lock_auth_base::auth_type auth2(new lc::lock_auth <lc::r_lock>);

  //proxy objects for accessing the protected data (use them like pointers)
  base::write_proxy write;
  base::read_proxy  read;

  //get a proxy, without deadlock prevention
  write = data0.get_write();
  assert(write); //(just for testing)
  //write to the object
  *write = 1;
  //release the lock
  write.clear();
  assert(!write);

  //get a proxy, with deadlock prevention
  write = data0.get_write_auth(auth);
  assert(write);
  //NOTE: this updates 'auth', since 'get_write_auth' was used!
  write.clear();

  //get a read-only proxy
  read = data0.get_read_auth(auth);
  assert(read);
  read.clear();

  //you can use the same proxy object with containers of the same base type
  read = data1.get_read_auth(auth);
  assert(read);

  {
    //'auth' still holds a read lock, but 'data0' isn't in use, so this should succeed
    base::write_proxy write2 = data0.get_write_auth(auth);
    assert(write2);

    //this is a potential deadlock, since 'auth' has a write lock and 'data1' is in use
    base::read_proxy read2 = data1.get_read_auth(auth);
    assert(!read2);
  } //<-- 'write2' goes out of scope, which unlocks 'data0'

  {
    //copy the proxy object
    base::read_proxy read2 = read;
    assert(read2);
  } //<-- 'read2' goes out of scope, but 'data1' doesn't get unlocked since it's not a new lock

  assert(read);
  read.clear();

  //use 'try_copy_container' to copy containers (attempts to lock both containers)
  bool success1 = try_copy_container(data0, data1, auth);
  assert(success1);

  //use 'try_copy_container' to copy containers, with multi-locking
  //NOTE: normally every 'get_write_auth' and 'get_read_auth' above should be
  //replaced with 'get_write_multi' and 'get_read_multi' so that 'multi_lock'
  //keeps track of all of the locks held on 'data0' and 'data1'. this is so that
  //'multi_lock' makes this call block until no other threads are accessing
  //'data0' or 'data1'. (see test.cpp more a more elaborate example.)
  lc::multi_lock multi;
  bool success2 = try_copy_container(data0, data1, multi, auth);
  assert(success2);

  //or, if this thread already holds a write lock on 'multi_lock'...
  lc::multi_lock::write_proxy multi_write = multi.get_write_auth(auth);
  bool success3 = try_copy_container(data0, data1, multi, auth, true, false);
  assert(success3);
}
