The header mutex-container.hpp contains the class 'mutex_container', which is a
template that protects a single object using mutexes. The contents of the
container are only available via proxy objects that ensure that the container
is locked and unlocked properly. The main purpose of the container is to
mitigate accidental failure to unlock the container when a thread is finished
using it.

There are a few lock types available. The default lock type allows multiple
threads to read from the container at once, but to write to the container the
thread must be the only one accessing it. A second lock type only allows one
thread to read or write at a time, but it should be more efficient when you
never want to allow multiple threads access to the container at one time.

See test.cpp for an example. You might also want to compile/run it to make sure
that it works properly on the target system.

Kevin P. Barry [ta0kira@gmail.com], 20150101
