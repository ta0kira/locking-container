/* This is both a sort of unit test, and a demonstration of how to use deadlock
 * prevention.
 *
 * Suggested compilation command:
 *   c++ -Wall -pedantic -std=c++11 test.cpp -o test -lpthread
 */

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "locking-container.hpp"

//use this definition if you want the simple test
#define THREAD_TYPE thread
//use this definition if you want the multi-lock test
//#define THREAD_TYPE thread_multi

//(probably better as arguments, but I'm too lazy right now)
#define THREADS 10
#define TIME    30
//(if you set either of these to 'false', the threads will gradually die off)
#define READ_BLOCK  true
#define WRITE_BLOCK true


//the data being protected (initialize the 'int' to 'THREADS')
typedef locking_container <int> protected_int;
static protected_int my_data(THREADS);

//(used by 'thread_multi')
static protected_int  my_data2;
static null_container multi_lock;

static void send_output(const char *format, ...);

static void *thread(void *nv);
static void *thread_multi(void *nv);


int main()
{
  //create some threads
  pthread_t threads[THREADS];
  for (long i = 0; (unsigned) i < sizeof threads / sizeof(pthread_t); i++) {
    send_output("start %li\n", i);
    threads[i] = pthread_t();
    if (pthread_create(threads + i, NULL, &THREAD_TYPE, (void*) i) != 0) {
      send_output("error: %s\n", strerror(errno));
    }
  }

  //wait for them to do some stuff
  sleep(TIME);

  //the threads exit when the value goes below 0
  {
    protected_int::proxy write = my_data.get();
    //(no clean way to exit if the container can't be locked)
    assert(write);
    *write = -1;
  } //<-- proxy goes out of scope and unlocks 'my_data' here (you can also 'write.clear()')

  for (long i = 0; (unsigned) i < sizeof threads / sizeof(pthread_t); i++) {
    send_output("?join %li\n", i);
    pthread_join(threads[i], NULL);
    send_output("+join %li\n", i);
  }
}


//a print function that ensures we have exclusive access to the output
static void send_output(const char *format, ...) {
  //protect the output file while we're at it
  typedef locking_container <FILE*, w_lock> protected_out;
  //(this is local so that it can't be involved in a deadlock)
  static protected_out stdout2(stdout);

  va_list ap;
  va_start(ap, format);

  //NOTE: authorization isn't important here because it's not possible for the
  //caller to lock another container while it holds a lock on 'stdout2';
  //deadlocks aren't an issue with respect to 'stdout2'
  protected_out::proxy write = stdout2.get();
  if (!write) return;
  vfprintf(*write, format, ap);
}


//a simple thread for repeatedly accessing the data
static void *thread(void *nv) {
  //(cancelation can be messy...)
  if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) return NULL;

  //get an authorization object, to prevent deadlocks
  //NOTE: for the most part you should be able to use any authorization type
  //with any lock type, but the behavior will be the stricter of the two
  protected_int::auth_type auth(protected_int::new_auth());

  long n = (long) nv, counter = 0;
  struct timespec wait = { 0, (10 + n) * 10 * 1000 * 1000 };
  nanosleep(&wait, NULL);

  //loop through reading and writing forever

  while (true) {
    //read a bunch of times

    for (int i = 0; i < THREADS + n; i++) {
      send_output("?read %li\n", n);
      protected_int::const_proxy read = my_data.get_auth_const(auth, READ_BLOCK);
      if (!read) {
        send_output("!read %li\n", n);
        return NULL;
      }

      send_output("+read %li (%i) -> %i\n", n, read.last_lock_count(), *read);
      send_output("@read %li %i\n", n, !!my_data.get_auth_const(auth, READ_BLOCK));
      if (*read < 0) {
      send_output("counter %li %i\n", n, counter);
        return NULL;
      }
      //(sort of like a contest, to see how many times each thread reads its own number)
      if (*read == n) ++counter;
      nanosleep(&wait, NULL);

      read.clear();
      send_output("-read %li\n", n);
      nanosleep(&wait, NULL);
    }

    //write once

    send_output("?write %li\n", n);
    protected_int::proxy write = my_data.get_auth(auth, WRITE_BLOCK);
    if (!write) {
      send_output("!write %li\n", n);
      return NULL;
    }

    send_output("+write %li (%i)\n", n, write.last_lock_count());
    send_output("@write %li %i\n", n, !!my_data.get_auth(auth, WRITE_BLOCK));
    if (*write < 0) {
      send_output("counter %li %i\n", n, counter);
      return NULL;
    }
    *write = n;
    nanosleep(&wait, NULL);

    write.clear();
    send_output("-write %li\n", n);
    nanosleep(&wait, NULL);
  }
}


//a more complicated thread that requires deadlock prevention, but multiple write locks at once
static void *thread_multi(void *nv) {
  if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) return NULL;

  protected_int::auth_type auth(protected_int::new_auth());

  long n = (long) nv;
  struct timespec wait = { 0, (10 + n) * 1 * 1000 * 1000 };
  nanosleep(&wait, NULL);

  while (true) {
    for (int i = 0; i < THREADS + n; i++) {
      send_output("?read0 %li\n", n);
      protected_int::const_proxy read0 = my_data.get_multi_const(multi_lock, auth);
      if (!read0) {
        send_output("!read0 %li\n", n);
        return NULL;
      }

      send_output("+read0 %li (%i) -> %i\n", n, read0.last_lock_count(), *read0);
      if (*read0 < 0) return NULL;
      nanosleep(&wait, NULL);

      send_output("?read1 %li\n", n);
      protected_int::const_proxy read1 = my_data2.get_multi_const(multi_lock, auth);
      if (!read1) {
        send_output("!read1 %li\n", n);
        //NOTE: due to deadlock prevention, 'auth' will reject a lock if another
        //thread is waiting for a write lock for 'multi_lock' because this
        //thread already holds a read lock (on 'my_data'). (this could easily
        //lead to a deadlock if 'get_multi_const' above blocked.) this isn't a
        //catastrophic error, so we just break out of this loop.
        break;
      }

      send_output("+read1 %li (%i) -> %i\n", n, read1.last_lock_count(), *read1);
      if (*read1 < 0) return NULL;
      nanosleep(&wait, NULL);

      read1.clear();
      send_output("-read1 %li\n", n);
      read0.clear();
      send_output("-read0 %li\n", n);
      nanosleep(&wait, NULL);

      send_output("?write %li\n", n);
      protected_int::proxy write = my_data.get_multi(multi_lock, auth);
      if (!write) {
        send_output("!write %li\n", n);
        //(this thread has no locks at this point, so 'get_multi' above should
        //simply block if another thread is waiting for (or has) a write lock on
        //'multi_lock'. a NULL return is therefore an error.)
        return NULL;
      }

      send_output("+write %li (%i)\n", n, write.last_lock_count());
      if (*write < 0) return NULL;
      *write = n;
      nanosleep(&wait, NULL);

      write.clear();
      send_output("-write %li\n", n);
      nanosleep(&wait, NULL);
    }

    //get a write lock on 'multi_lock'. this blocks until all other locks have
    //been released (provided they were obtained with 'get_multi' or
    //'get_multi_const' using 'multi_lock). this is mostly a way to appease
    //'auth', because it's preventing deadlocks.

    //NOTE: the lock will be rejected without blocking if this thread holds a
    //lock on another object, because a deadlock could otherwise happen!

    send_output("?multi0 %li\n", n);
    null_container::proxy multi = multi_lock.get_auth(auth);
    if (!multi) {
      send_output("!multi0 %li\n", n);
      return NULL;
    }
    send_output("+multi0 %li\n", n);

    //NOTE: you can't use 'get_multi' or 'get_multi_const' while 'multi_lock' is
    //locked! this is because 'multi_lock' won't allow new read locks while this
    //thread holds a write lock on it.

    send_output("?multi1 %li\n", n);
    protected_int::proxy write1 = my_data.get_auth(auth);
    if (!write1) {
      send_output("!multi1 %li\n", n);
      return NULL;
    }
    send_output("+multi1 %li\n", n);
    if (*write1 < 0) return NULL;

    send_output("?multi2 %li\n", n);
    protected_int::proxy write2 = my_data2.get_auth(auth);
    if (!write2) {
      send_output("!multi2 %li\n", n);
      return NULL;
    }
    send_output("+multi2 %li\n", n);

    *write1 = *write2 = 100 + n;

    write2.clear();
    send_output("-multi2 %li\n", n);
    write1.clear();
    send_output("-multi1 %li\n", n);

    //NOTE: since you can't use 'get_multi' above, 'multi_lock' can't track the
    //new locks that are made on 'my_data' and 'my_data2'; therefore,
    //'multi_lock' must stay locked until the other locks are released.
    //(otherwise, 'multi_lock' could give a write lock to another thread while
    //'my_data' or 'my_data2' are still locked.)
    multi.clear();
    send_output("-multi0 %li\n", n);
  }
}
