Please note that this is only designed for POSIX systems (e.g., Linux, OS X,
and FreeBSD)! This almost certainly won't even compile for other system types.
It also requires a compiler that supports C++11 features.

The header locking-container.hpp contains the class 'locking_container', which
is a template that protects a single object. The contents of the container are
only available via proxy objects that ensure that the container is locked and
unlocked properly. The main purpose of the container is to mitigate accidental
failure to unlock the container when a thread is finished using it. A secondary
objective is to isolate the locking logic from the container logic.

There are a few lock types available. The default lock type allows multiple
threads to read from the container at once, but to write to the container the
thread must be the only one accessing it. A second lock type only allows one
thread to read or write at a time, but it should be more efficient when you
never want to allow multiple threads access to the container at one time. A
third type of lock allows multiple readers, but no writers.

This container also supports deadlock prevention via authorization objects. The
basic idea is that each thread creates its own authorization object that will
keep track of the locks held by the thread. This will prevent deadlocks, at the
possible expense of being overly-cautious with lock rejection, by rejecting a
lock request if it's possible for a deadlock, even when working with multiple
containers. Each lock type has a corresponding authorization object type that
mirrors the lock type's behavior. (For example, some authorization objects allow
a thread to hold multiple read locks, whereas others only allow a single read
lock.) It is important to note that the authorization objects don't track which
containers the thread holds a lock for! Authorization objects are sensitive to
whether or not a container is currently in use, however.

Deadlock prevention will by default prevent a thread from holding multiple write
locks at once, or a write lock and one or more read locks at once. This isn't
always practical, however. The class 'null_container' is designed to work around
this, without compromising deadlock prevention. Essentially, a single
'null_container' is used to keep track of all of the read and write locks that
are currently being held on all objects. When a thread wants to access multiple
containers at once, it releases all of its current locks and requests a write
lock for the 'null_container'. The lock is granted when no other thread has a
lock on any of the objects in question. (New locks on other objects are blocked
while the thread is waiting for a write lock.) This allows the thread to obtain
multiple write locks, as long as they are all for different objects. This
effectively silences all other threads while the thread in question takes its
pick of objects to lock. (See 'thread_multi' in test.cpp for an example.)

See simple.cpp for example usage in a single thread. test.cpp contains examples
with multiple threads, to include multiple write locks with deadlock prevention.

This software is released under the BSD License. See the individual header files
for more information.

Kevin P. Barry [ta0kira@gmail.com], 20150103
