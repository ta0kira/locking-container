***** Overview *****

This project provides several novel solutions to common deadlocking problems in
multithreaded C++ applications. The philosophy of this project is that it's
better to have an identifiable failure point than to have a mysterious deadlock
that can't be located.

This project uses C++11, so this code requires an appropriate compiler.

To utilize this project, move the contents of the "include" directory to a
convenient location, then:

  #include "locking-container.hpp"

In order to be minimally obtrusive, the non-template sources are located in
"locking-container.inc"; you must include that file in at least one of your own
source files to get the definitions of the non-template class functions.


***** Background *****

Deadlocking in multithreaded applications is inconvenient, to say the least. If
nothing else, it can be difficult to tell if you actually have a deadlock, or if
you've managed to hang on some other blocking system call. This project only
addresses deadlocks caused by intentionally blocking for access to an object
residing in the program's memory; things that would normally be protected by a
mutex. There are a few predictable causes of deadlocks in this situation:

  - Locking a mutex in a function, but forgetting to unlock it; perhaps you
    missed a return statement somewhere.

  - Multiple threads waiting for access to resources that are already locked,
    where each of those threads is locking out another thread.

This project addresses both of these situations. The first is fairly simple to
solve: Create a container that protects an object, and only provides access to
it via a proxy object that locks the mutex upon construction and unlocks it upon
destruction.

The second situation is more complicated to deal with. To start with, it helps
immensely knowing whether or not a lock attempt would block before even
attempting to do so. Sure, 'pthread_mutex_trylock' will do this, but if you
wanted to first find out if the call would block, and then optionally allow it
to block, this would cause a race condition. A better solution is to store some
non-trivial lock information that can be checked prior to blocking for a lock.

Unfortunately, checking the lock state before blocking on a mutex also causes a
race condition. Instead of blocking on a mutex, we can protect access to the
lock's state using a mutex, and while it's locked, we can check the state,
change it, call other functions, etc. while deciding if it's best to block for
access to the lock. If it's decided that there will be no blocking, we simply
unlock the mutex and return an error. If there will be blocking, however, we can
block on a condition variable. This will implicitly unlock the mutex (allowing
other callers to check/update the lock's state), and lock it again once the
condition has been released. In both cases, the mutex protecting the lock's
state information stays locked for a comparatively short period of time. Sparing
you the details, this allows us to create more sophisticated locking types and
locking behaviors.


***** Concurrency Problems *****

This project attempts to solve several concurrency problems at the same time:

  1) Multiple Readers: In some cases, several threads merely want to read data
     that's being protected by a lock. Is there a way to allow multiple readers
     at once, without compromising data integrity?

  2) Deadlock Prevention: What reliable ways are there to detect and prevent
     deadlocks, without too much of a performance cost?

  3) Multiple Locks: Some deadlock prevention solutions restrict the number of
     locks that a thread can hold at once; this project is no exception. Are
     there reliable ways to allow a thread to hold multiple locks without
     causing deadlocks?

  4) Integration: How can all of the above be integrated with a locking system
     that is as automated as possible?

This project addresses all of these problems, one step at a time.


***** Solutions *****

The first step in solving the problems above is to generalize the container, the
locking mechanisms, and the deadlock-prevention mechanisms. The class
'lc::locking_container' and its base class 'lc::locking_container_base', provide
this generalization by allowing you to specify the type of object to protect,
and the type of lock you want protecting it. For example:

  typedef lc::locking_container <int, lc::rw_lock> int_rw;
  int_rw my_int;

...will protect an 'int' using an 'lc::rw_lock'. There are numerous lock types
available, but the base class 'lc::locking_container_base <int>' allows 'int'
containers with different lock types to be interchangeable.

  typedef lc::locking_container_base <int> int_base;


----- Container Access -----

Accessing the contents of the container is very straightforward:

  int_base::read_proxy  read  = my_int.get_read();

...or:

  int_base::write_proxy write = my_int.get_write();

(Note that doing one after the other will cause a deadlock in most cases, since
you'd be attempting two incompatible locks on the same container!)

  if (!write) /*access error*/;
  else *write = 1;

If locking is successful, 'read' and/or 'write' will act as smart pointers to
provide access to the contents of 'my_int'. You should check for 'NULL', then
proceed to use them like pointers. When they go out of scope, or when you call

  read.clear();
  write.clear();

...the respective lock on 'my_int' will be released. That's all there is to it!
(But, see the very end for Scoping Concerns.) Also, any of the "get" functions
discussed in this document take an optional 'bool' argument that indicates
blocking preference. Set this to 'false' if you don't want to block for a lock.
Note that there might still be a slight block while waiting for access to the
status information stored by the lock.

Note that the proxy objects act like shared pointers, and the lock isn't
released until the reference count hits zero. This means that if you 'clear' a
proxy, there might still be other references to it that keep the lock from being
released. This behavior can be useful (vs. 'std::unique_ptr' behavior) if you
want to organize the proxy objects, e.g., in a list or a queue.


----- Lock Types -----

There are several choices of locks available. Your choice of lock will really
depend on the application. The more sophisticated locks are less efficient, but
the more efficient locks lack versatility.

'lc::rw_lock': This is the default lock type, which addresses objective 1)
listed above. This type of lock will allow multiple threads to access the
contained object for reading, provided no thread is accessing it for writing. If
a thread requests write access, further read access is blocked until the thread
receives and subsequently returns write access to the object.

'lc::r_lock': This lock type will allow multiple threads read access, but it
will never allow a thread write access. This type of lock will never block.
Even though a deadlock cannot happen directly using this deadlock, it can be
involved in a deadlock if multi-locking is used (discussed later on).

'lc::w_lock': This lock type will allow only one thread access at any given
time, regardless of if it's for reading or writing. This lock is more efficient
than 'lc::rw_lock', but it isn't the most efficient.

'lc::dumb_lock': This is the familiar mutex lock we all know and love; the one
that isn't capable of preventing deadlocks. This lock behaves similarly to
'lc::w_lock', except it doesn't store state information, and therefore it is
incapable of detecting potential deadlocks. It is, however, the most efficient
of the blocking locks. ('lc::r_lock' is more efficient, but it's not a real
lock.)

'lc::broken_lock': This lock is only for testing purposes. It universally denies
locks to all callers 100% of the time.

With all of the above lock types, accessing the data protected by the container
is exactly the same. What differs between them is under what circumstances the
access request succeeds, fails, or blocks. They differ even more when deadlock
prevention is used, discussed below.

Multi-read locks (e.g., 'lc::rw_lock' and 'lc::r_lock') should never be used to
protect objects that aren't thread-safe when 'const'. This is because those
locks allow multiple threads to call 'const' functions on the objects; if they
aren't thread-safe when 'const', this could cause data corruption. Examples of
types that should not be protected with multi-read locks are: pointers; objects
whose 'const' functions call non-reentrant global functions; objects with
mutable members.


----- Deadlock Prevention -----

At this point, the Multiple Readers problem is solved; however, deadlocks are
still an issue here, because we haven't discussed what happens when a thread
obtains multiple locks at once. Take the following example:

  int_base::read_proxy  read  = my_int.get_read();
  int_base::write_proxy write = my_int.get_write();

Recall that 'my_int' is protected by 'lc::rw_lock', which means that it allows
multiple readers at once, but not a reader and a writer at once. The problem
here is not that we've requested multiple locks; it's that the same thread has
requested a second lock that is being locked out by the thread's first lock. The
solution here is to keep track of the locks that this thread has so that a
deadlock can be prevented. Note that there is a trade-off here: A deadlock is
prevented by causing an error that must subsequently be dealt with.

It's important to note that the deadlock-prevention strategy used by this
project is to reject a lock that might otherwise immediately result in a
deadlock. The keyword here is "immediately"; it's only the last lock operation
in the deadlock that counts.

Lock tracking is done via authorization objects. These objects record important
information regarding the locks a thread obtains, to be used when requesting
subsequent locks to prevent deadlocks. You can create an authorization object as
follows:

  //specific to the lock type of 'my_int'
  lc::lock_auth_base::auth_type auth(my_int.get_new_auth());

...or:

  //specific to the lock type of 'int_rw'
  lc::lock_auth_base::auth_type auth(int_rw::new_auth());

...or:

  //specific to the 'lc::rw_lock' lock type
  lc::lock_auth_base::auth_type auth(new lc::lock_auth <lc::rw_lock>);

As you might have guessed, there is a corresponding authorization type for each
lock type. (Actually, all of the authorization types work with all of the lock
types, but the behavior becomes the more restrictive of the two.)

Given the authorization object 'auth' created above (they are all the same, in
this case), we can now track all of the locks obtained by this thread:

  int_base::read_proxy  read  = my_int.get_read_auth(auth);
  int_base::write_proxy write = my_int.get_write_auth(auth);

If the first line results in a successful lock, 'auth' will contain information
about that lock. When the second line is called, the lock protecting 'my_int'
will pass information to 'auth' to determine if a deadlock is possible. Since a
deadlock is certain in this case, and because authorization objects err on the
side of caution, the second line should result in 'NULL'. Thus, a deadlock has
been prevented, deferring to the programmer to properly handle the error.

Note that the authorization objects don't track the actual identities of the
containers that it has locked. That would be helpful, but to be useful we would
also need to implement more sophisticated deadlock-detection algorithms, which
could make the whole system less efficient. (You might be able to create an
authorization class that does this, however! I just haven't done so.)

Authorization objects are not thread-safe; therefore, a single authorization
object should only be used with one thread. In other words, each authorization
object is intended to be "owned" by a particular thread.


----- Authorization Types -----

Each of the locks mentioned above has a 'lc::lock_auth' specialization that
has behavior that mirrors the corresponding lock type. The 'get_new_auth'
virtual function and the 'new_auth' static function are available from
'locking_container_base' objects and 'locking_container' (respectively) to
create an authorization object corresponding to the lock type of a particular
container or container type. Alternatively, you can explicitly create an
authorization object using 'new'. In all cases, the authorization object must be
stored as a 'lc::lock_auth_base::auth_type', which is simply a shared pointer.

Each of the authorization types below imposes lock-count restrictions that
mirror those of the corresponding lock type. These restrictions are only part of
what provides deadlock prevention.

'lc::lock_auth <lc::rw_lock>': Mirroring 'lc::rw_lock', this auth. object allows
the thread to hold multiple read locks, or a single write lock, but not both.

'lc::lock_auth <lc::r_lock>': Mirroring 'lc::r_lock', this auth. object allows
the thread to hold multiple read locks, but no write locks. This is useful if
you want to prevent a thread from obtaining a write lock on any object.

'lc::lock_auth <lc::w_lock>': Mirroring 'lc::w_lock', this auth. object allows
the thread to hold only a single lock at once. This is useful for threads that
will only be using write locks.

'lc::lock_auth <lc::dumb_lock>': Mirroring 'lc::dumb_lock', this auth. object
allows the thread to hold only a single lock at once. Unlike the auth. object
above (corresponding to 'lc::w_lock'), the exceptions below don't apply! This
auth. type is useful if you absolutely never want a particular thread to hold
more than one lock on a blocking container (i.e., excluding 'lc::r_lock
containers) at one time.

'lc::lock_auth <lc::broken_lock>': Mirroring 'lc::broken_lock', this auth.
object denies all locks all the time. This is useful if you want to see how a
thread responds to being unable to lock any containers.

The above restrictions are specifically meant to mirror the corresponding lock
types. For the first three types above (but not the latter two), additional
exceptions are made:

  1) "Lockout" Exception: If another thread is waiting for a lock on the
     container to be locked, and the calling thread holds a lock on any
     container, a deadlock is possible; therefore, the lock will be rejected,
     even if it wouldn't violate lock-count restrictions imposed by the auth.
     object. This is where most of the deadlock prevention happens. (In some
     cases, the authorization object will request a non-blocking lock attempt
     rather than rejecting the lock. This is only useful with 'lc::dumb_lock',
     which never really knows if a thread is waiting.)

  2) "Must Block" Exception: If the lock operation doesn't need to block, a
     deadlock isn't possible; therefore, the lock will be allowed, even if it
     violates lock-count restrictions otherwise imposed by the auth. object.
     This allows threads to potentially hold multiple read locks, which is
     addressed in detail in a later section.

  3) "Writer Reads" Exception: If the container to be locked uses 'lc::rw_lock',
     and the calling thread holds the write lock on that container and it's
     requesting a read lock, the lock will be allowed, even if it violates
     lock-count restrictions otherwise imposed by the auth. object. count
     restrictions, or the "Lockout" Exception above.

Note that the first two exceptions are not possible with 'lc::dumb_lock' locks
because the operation will always potentially block, and there is no way to know
if another thread is already blocking for a lock.

Given the exceptions above, the main difference between the authorization types,
as far as blocking, relates to what happens when the call must block and no
other thread is currently locked out. In such a situation, if the caller holds a
write lock (according to the authorization object) or a write lock is currently
being requested, the lock will be rejected. Otherwise (i.e., the caller only
holds read locks and read lock is being requested), the call will be allowed to
block. The difference in behavior therefore comes down to whether or not the
authorization object tracks read and/or write locks.


----- Multiple Locks -----

So, it seems that the deadlock prevention approach used by this project is
fairly restrictive. This is helpful for preventing mysterious application
freezes, but in some cases it prevents functionality that is absolutely
essential for some applications to function properly. In particular, a thread
might need to hold multiple locks at once, with at least one of them being a
write lock. This project supports three solutions, not including the "just hope
there isn't a deadlock" approach. Each of the solutions has its merits and
drawbacks:

  1) Use authorization objects as usual, and if a lock request is denied,
     release all of the locks held, take a nap, and try again in a few ms. This
     is suitable in situations where it isn't likely that an object is going to
     be locked already, e.g., applications that only occasionally lock
     containers.

  2) Use the multi-locking technique provided by this project. This involves
     having a "master lock" that must be locked if a thread wants to hold
     multiple write locks, etc. When no thread needs multiple write locks, this
     functions the same as above. When a thread wants multiple write locks, it
     waits for a lock on the master lock, which is only granted after all other
     threads release all of their locks. The advantage of this method is that
     you generally don't have to loop while waiting for multiple locks. The
     disadvantage is that every other thread must stop briefly while the multi-
     locking thread selects what it wants to lock, but everything can proceed
     again once those locks are selected. This method is appropriate when there
     is no reasonable way to order objects (e.g., in a graph), or when all of
     the necessary locks can be obtained prior to performing operations on the
     respective objects.

  3) Use ordered locks, where each container has a numerical order that
     determines what containers can be locked after it. If a thread has a lock
     on a container that has order x, it will be unconditionally allowed to
     block for a lock on any container with order x+1 or higher. This prevents
     deadlocks because any other thread holding a lock on a container with order
     x+1 or higher can't be waiting for a lock on a container with order x or
     lower. This is very reliable and efficient, but it requires you to
     establish a container ordering that must be respected throughout your code.
     This is most appropriate for high-speed applications that are continually
     locking a large number of objects.

Suppose we have the following two global objects:

  typedef lc::locking_container <int, lc::rw_lock> int_rw;
  typedef lc::locking_container_base <int>         int_base;
  int_rw my_int0, my_int1;

...and we want to hold write locks on both 'my_int0' and 'my_int1' at the same
time. The examples below demonstrate each of the solutions applied to this
situation.


,,,,, Solution 1 ,,,,,

The first solution is to simply use authorization objects as usual:

  lc::lock_auth_base::auth_type auth(int_rw::new_auth());

  int n; //(see below)

  //...

  for (int tried = 0; true; tried++) {
    if (tried && (tried + n) % 2) /*'nanosleep', etc.*/;

    int_base::write_proxy write0 = my_int0.get_write_auth(auth);
    if (!write0) /*probably a fatal error*/;

    int_base::write_proxy write1 = my_int1.get_write_auth(auth);
    if (!write1) {
      continue;
    } else {
      //both locks are fine: proceed with the operation
      break;
    }
  }

This solution repeatedly attempts to obtain both locks. If the second lock
fails, the assumption is that it's because 'my_int1' is already locked; hence,
the thread with that lock could itself be waiting for a lock on 'my_int0', which
would cause a deadlock. It's very important to note that the lock on 'my_int0'
must be released, to be certain that a deadlock isn't going to happen. If you
instead kept looping until the lock on 'my_int1' succeeded, you might end up
manually recreating the deadlock that 'auth' is preventing!

Another important part of this solution is the delay before retrying. This delay
is at the beginning of the loop, when no locks are held, which allows other
threads to do what they will with 'my_int0', under the assumption that that
might have been the reason 'my_int1' was in use the last time around. On the
other hand, you could have an unfortunate coincidence of identical threads that
keep blocking each other out and sleeping in unison; therefore, you should skip
a sleep occasionally, in order to cause the threads to become out of sync with
each other (e.g., set 'n' above to a thread number).


,,,,, Solution 2 ,,,,,

The second solution is possibly overkill in the other direction. Suppose, in
addition to the global objects 'my_int0' and 'my_int1', there is a global
'lc::meta_lock' object that serves as the master lock:

  lc::meta_lock master_lock;

Solution 1 would be modified as follows to implement the multi-locking solution:

  lc::lock_auth_base::auth_type auth(int_rw::new_auth());

  //...

  lc::meta_lock::write_proxy multi = master_lock.get_write_auth(auth);
  if (!multi) /*probably a fatal error*/;

  int_base::write_proxy write0 = my_int0.get_write_multi(master_lock, auth);
  if (!write0) /*probably a fatal error*/;

  int_base::write_proxy write1 = my_int1.get_write_multi(master_lock, auth);
  multi.clear(); //<-- release the master as soon as possible!
  if (!write1) /*probably a fatal error*/;

  //both locks are fine: proceed with the operation

Much like Solution 1, this solution "hopes" that 'my_int1' will not be in use by
any other threads. In this case, however, 'master_lock' ensures that no other
thread holds a lock, which is why we don't need a loop to keep retrying the lock
requests. If they fail, it's not going to get any better.

The most important point to make regarding this solution is that it requires
that all other lock operations use 'get_read_multi' and 'get_write_multi' with
the same 'lc::meta_lock' object! Even for operations that don't require multiple
write locks, or a write lock with one or more read locks. The entire purpose of
the master lock is to ensure that all other locks are released when a thread
requests a lock on the master lock.

Here is a more detailed explanation of how it works. For normal read and write
operations, we simply use:

  int_base::write_proxy write0 = my_int0.get_write_multi(master_lock, auth);

  //...

  int_base::read_proxy read0 = my_int0.get_read_multi(master_lock, auth);

Every time this sort of lock is made, a read lock is taken out on 'master_lock'.
Since 'lc::meta_lock' contains a 'lc::rw_lock', 'master_lock' can handle and
unlimited number of read locks at once. Now, suppose a thread wants a multi-
lock, which is obtained using a write lock on 'master_lock':

  lc::meta_lock::write_proxy multi = master_lock.get_write_auth(auth);

Here we are requesting a write lock on a object with a 'lc::rw_lock'. When this
happens, the call blocks until there are no readers, and new read operations are
blocked out until the write lock is granted and returned. Importantly, 'auth'
must have no locks at the time the write lock on 'master_lock' is requested!
This is because it would almost certainly cause a deadlock. Suppose another
thread requests a new lock while this thread is waiting for a write lock on
'master_lock':

  int_base::read_proxy read1 = my_int1.get_read_multi(master_lock, auth2);

Here, a read lock is requested for 'master_lock', since that's how the whole
multi-locking thing works. What happens here depends on what else 'auth2' is
associated with at the time. If 'auth2' has other locks, then this call should
fail, because it's very possible that the other lock held by 'auth2' is what's
causing the write lock on 'master_lock' to block. In that case, we need to go
back to Solution 1 by clearing all locks held, waiting, and retrying. In other
words, with the multi-locking technique there are potential (but prevented!)
deadlocks even when you're requesting multiple read locks, so your code should
allow for retrying if a second read lock fails. On the other hand, if 'auth2'
currently holds no other locks, the call above should block until the multi-lock
operation completes and the write lock on 'master_lock' is released.

For the multi-locking thread, it is very important for it to release the write
lock as soon as it obtains all of the locks it needs, i.e., before operating on
the objects that it's obtained locks for. This is so that other threads can
proceed to access the containers that the multi-locking thread hasn't locked,
and so they can access the locked containers immediately when they become free
again.

Alternatively, suppose you don't want a multi-lock, but you want to wait until
no multi-lock is waiting, and then prevent a new multi-lock from happening. This
is useful in cases where the the 'lc::meta_lock' actually serves as a lock for
some larger aggregate structure that isn't stored in a 'lc::locking_container'.
(See example/graph-multi.cpp for a specific example.) To do this, you request a
read lock, rather than a write lock:

  lc::meta_lock::read_proxy protect_read = master_lock.get_read_auth(auth);

Provided 'auth' holds no other locks, this will block until there are no write
locks pending/held on 'master_lock'. Note that the only protection provided here
is temporarily locking out multi-locks; this functionality is provided so that
you can lock out such operations when you don't actually need to take out a lock
on any other object.


,,,,, Solution 3 ,,,,,

This is the most computationally efficient solution, but it's also the most
demanding of the programmer. This solution requires that the lock types used be
wrapped with 'lc::ordered_lock', e.g., 'lc::ordered_lock <lc::rw_lock>'. The
modified lock type is nearly identical to the original lock type, except that
each container must be assigned an order, and ordered authorization objects must
always be used when obtaining locks.

  typedef lc::ordered_lock <lc::rw_lock>          rw_ordered;
  typedef lc::locking_container <int, rw_ordered> int_rw_ordered;
  typedef lc::locking_container_base <int>        int_base;
  int_rw_ordered my_int0(0, 1), my_int1(0, 2);

Above, the first argument to 'int_rw_ordered::int_rw_ordered' is the object
initializer, and the second is the lock order, passed to the lock object. This
must be > 0 for ordering to work. If the order is 0, the lock functions like the
original lock type (e.g., 'lc::rw_lock'), except that ordered authorization
objects must still be used. 'get_read' and 'get_write' will return 'NULL' for
ordered locks because the system only works if we can be sure that all threads
abide by the ordering rules (via authorization objects).

If we know that we're locking containers in order, all we have to do is use
the usual authorization method:

  lc::lock_auth_base::auth_type auth(int_rw_ordered::new_auth());

  //...

  int_base::write_proxy write0 = my_int0.get_write_auth(auth);
  if (!write0) /*probably a fatal error*/;

  int_base::write_proxy write1 = my_int1.get_write_auth(auth);
  if (!write1) /*probably a fatal error*/;

If either of the above operations fail, it's almost certainly because we forgot
about another lock of higher order that 'auth' currently holds. Hence, this is
an actual failure, and we don't loop to retry the lock requests.

In certain cases, you might need to lock two containers out of order. If a lock
is attempted out of order, the normal locking rules apply. In this case, the
solution is exactly the same as Solution 1: Try both locks, and if it fails,
release both of them, take a nap, and try again.

Authorization objects corresponding to ordered locks behave similarly to their
unordered counterparts, except they are more liberal about allowing locks when
ordering rules are respected; the restrictions are a subset of those used for
unordered locks. If an ordered authorization object is used to lock an unordered
lock, or an ordered lock with order 0, the authorization will revert to the more
restrictive unordered rules until the unordered lock is released. In general,
when a thread uses ordered locking, the authorization object allows all lock
requests to block, regardless of locks currently held. (The two exceptions are
with the auth. objects corresponding to 'lc::dumb_lock' and 'lc::broken_lock'.)


***** Scoping Concerns *****

There are a few things you need to know about the scopes of locks, proxy
objects, and authorization objects. Due to the automation provided by this
project, it's possible for a container/lock to be destructed before a proxy that
holds a lock on it, or for an authorization object to be destructed while it
still has a lock tied to it. For example:

  {
    typedef lc::locking_container <int> int_default;
    int_default::write_proxy write;
    int_default my_int;
    write = my_int.get_write();
  } //<-- danger! 'write' gets destructed after 'my_int'!

Or:

  {
    typedef lc::locking_container <int> int_default;
    int_default my_int;
    int_default::write_proxy write;
    lc::lock_auth_base::auth_type auth(my_int.get_new_auth());
    write = my_int.get_write_auth(auth);
  } //<-- danger! 'write' gets destructed after 'auth'!

The above situations will cause assertions, if you haven't disabled them. Either
way, you must be aware that these situations are bad:

  - A lock destructs while it's still locked.

  - An authorization object destructs while it has a lock associated with it.

Both of these can be mitigated by forcing proxy objects to destruct first when
they would otherwise be destructed after the corresponding lock or authorization
object:

  {
    typedef lc::locking_container <int> int_default;
    int_default::write_proxy write;
    int_default my_int;
    write = my_int.get_write();
    //...
    write.clear(); //<-- the magic
  }

Or:

  {
    typedef lc::locking_container <int> int_default;
    int_default my_int;
    int_default::write_proxy write;
    lc::lock_auth_base::auth_type auth(my_int.get_new_auth());
    write = my_int.get_write_auth(auth);
    //...
    write.clear(); //<-- the magic
  }


***** Conclusion *****

This document contains a lot of information, but without a lot of application.
If you want a simple example of the general procedure for using this project,
see "example/simple.cpp". If you want to see a more practical example, with all
of the discussed functionality demonstrated in one place, see "test/unit.cpp".
The latter is both a unit test and a demonstration of each of the deadlock
prevention techniques. To compile and run the unit test, run the script "test/
unit.sh". This will compile "test/unit.cpp" and run it under a variety of
conditions, some of which purposely cause deadlocks.


***** THE END *****

Kevin P. Barry [ta0kira@gmail.com], 20150107
