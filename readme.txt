Please note that this is only designed for POSIX systems (e.g., Linux, OS X,
and FreeBSD)! This almost certainly won't even compile for other system types.
It also requires a compiler that supports C++11 features.

The header mutex-container.hpp contains the class 'mutex_container', which is a
template that protects a single object using mutexes. The contents of the
container are only available via proxy objects that ensure that the container
is locked and unlocked properly. The main purpose of the container is to
mitigate accidental failure to unlock the container when a thread is finished
using it. A secondary objective is to isolate the locking logic from the
container logic.

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
lock. None of them allow a thread to hold multiple write locks.) It is important
to note that the authorization objects don't track which containers the thread
holds a lock for! From the perspective of authorization, attempting to lock a
second container is the same as attempting to lock the same container again.

See test.cpp for an example. You might also want to compile/run it to make sure
that it works properly on the target system.

Kevin P. Barry [ta0kira@gmail.com], 20150101
