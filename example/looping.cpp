/* This is mostly a test of deadlock prevention with loops that use both
 * multiple read locks at once, and write locks. This definitely has way too
 * much hard-coding; eventually I'll fix that and use command-line arguments.
 *
 * Suggested compilation command:
 *   c++ -Wall -pedantic -std=c++11 -O2 -I../include looping.cpp -o looping -lpthread
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
//(necessary for non-template source)
#include "locking-container.inc"

//(probably better as arguments, but I'm too lazy right now)
#define THREADS 10
#define TIME    30
//(if you set either of these to 'false', the threads will gradually die off)
#define READ_BLOCK  true
#define WRITE_BLOCK true
//TODO: make these into command-line arguments (specifically, the latter two)
#define LOCK_TYPE0 lc::rw_lock
#define LOCK_TYPE1 lc::rw_lock
#define AUTH_TYPE0 lc::r_lock
#define AUTH_TYPE1 lc::w_lock


//the data being protected (initialize the 'int' to 'THREADS')
typedef lc::locking_container_base <int> protected_int;
lc::locking_container <int, LOCK_TYPE0> my_data0(THREADS);
lc::locking_container <int, LOCK_TYPE1> my_data1;

static void send_output(const char *format, ...);

static void *thread(void *nv);


int main()
{
  //create some threads
  pthread_t threads[THREADS];
  for (long i = 0; (unsigned) i < sizeof threads / sizeof(pthread_t); i++) {
    send_output("start %li\n", i);
    threads[i] = pthread_t();
    if (pthread_create(threads + i, NULL, &thread, (void*) i) != 0) {
      send_output("error: %s\n", strerror(errno));
    }
  }

  //wait for them to do some stuff
  sleep(TIME);

  //the threads exit when the value goes below 0
  {
    protected_int::write_proxy write = my_data0.get_write();
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
  //(there is no reasonable reason to allow multiple locks at once.)
  typedef lc::locking_container <FILE*, lc::dumb_lock> protected_out;
  //(this is local so that it can't be involved in a deadlock)
  static protected_out stdout2(stdout);

  va_list ap;
  va_start(ap, format);

  //NOTE: authorization isn't important here because it's not possible for the
  //caller to lock another container while it holds a lock on 'stdout2';
  //deadlocks aren't an issue with respect to 'stdout2'
  protected_out::write_proxy write = stdout2.get_write();
  if (!write) return;
  vfprintf(*write, format, ap);
}


//a simple thread for repeatedly accessing the data
static void *thread(void *nv) {
  //(cancelation can be messy...)
  if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) return NULL;

  //NOTE: normally only one auth. should be used; this is just to simulate
  //different threads having different auth. types
  lc::lock_auth_base::auth_type auth0(new lc::lock_auth <AUTH_TYPE0>);
  lc::lock_auth_base::auth_type auth1(new lc::lock_auth <AUTH_TYPE1>);

  long n = (long) nv, match = 0, fail_r0 = 0, fail_w1 = 0, fail_r2 = 0, success_r2 = 0;
  struct timespec wait = { 0, (10 + n) * 10 * 1000 * 1000 };
  nanosleep(&wait, NULL);

  //loop through reading and writing forever

  while (true) {
    //read a bunch of times

    for (int i = 0; i < THREADS + n; i++) {
      send_output("?read1 %li\n", n);
      protected_int::read_proxy read1 = my_data1.get_read_auth(auth0, READ_BLOCK);
      if (!read1) {
        send_output("!read1 %li\n", n);
        return NULL;
      }

      send_output("+read1 %li (%i) -> %i\n", n, read1.last_lock_count(), *read1);
      nanosleep(&wait, NULL);

      //NOTE: this should block unless a writer is being locked out (note that
      //if 'lc::w_lock' is used, a read request uses a write lock)
      send_output("?read0 %li\n", n);
      protected_int::read_proxy read0 = my_data0.get_read_auth(auth0, READ_BLOCK);
      if (!read0) {
        ++fail_r0;
        send_output("!read0 %li\n", n);
      } else {
        send_output("+read0 %li (%i) -> %i\n", n, read0.last_lock_count(), *read0);
        if (*read0 < 0) break;
        //(sort of like a contest, to see how many times each thread reads its own number)
        if (*read0 == 0) ++match;
        nanosleep(&wait, NULL);

        //if a writer is waiting, this is really asking for a (potential) deadlock
        send_output("?read2 %li\n", n);
        protected_int::read_proxy read2 = my_data0.get_read_auth(auth0, READ_BLOCK);
        if (!read2) {
          ++fail_r2;
          send_output("!read2 %li\n", n);
        } else {
          ++success_r2;
          send_output("+read2 %li\n", n);
          read2.clear();
          send_output("-read2 %li\n", n);
        }

        read0.clear();
        send_output("-read0 %li\n", n);
      }

      read1.clear();
      send_output("-read1 %li\n", n);
      nanosleep(&wait, NULL);
    }

    //NOTE: pretend like 'auth1' was being used all along; this just simulates
    //other threads that are using different auth. types, and it allows testing
    //of 'lc::lock_auth <lc::r_lock>' in the loop above

    assert(!auth0->reading_count() && !auth0->writing_count());

    //write once

    send_output("?write0 %li\n", n);
    protected_int::write_proxy write0 = my_data0.get_write_auth(auth1, WRITE_BLOCK);
    if (!write0) {
      send_output("!write0 %li\n", n);
      return NULL;
    }

    send_output("+write0 %li (%i)\n", n, write0.last_lock_count());
    if (*write0 < 0) {
      send_output("counters %li %i %i %i %i %i\n", n, match,
        fail_r0, fail_w1, fail_r2, success_r2 );
      return NULL;
    }
    *write0 = n;
    nanosleep(&wait, NULL);

    write0.clear();
    send_output("-write0 %li\n", n);

    //(switch to a read lock to see how the auth. behaves with read and write locks)

    send_output("?read3 %li\n", n);
    //NOTE: make sure this is 'auth1' used with 'my_data0'!
    protected_int::read_proxy read3 = my_data0.get_read_auth(auth1, READ_BLOCK);
    if (!read3) {
      send_output("!read3 %li\n", n);
      return NULL;
    }

    send_output("+read3 %li\n", n);

    //NOTE: this will never block because 'auth' already holds a write lock
    send_output("?write1 %li\n", n);
    protected_int::write_proxy write1 = my_data1.get_write_auth(auth1, WRITE_BLOCK);
    if (!write1) {
      ++fail_w1;
      send_output("!write1 %li\n", n);
    } else {
      *write1 = *read3;
      nanosleep(&wait, NULL);
      write1.clear();
      send_output("-write1 %li\n", n);
    }

    read3.clear();
    send_output("-read3 %li\n", n);
    nanosleep(&wait, NULL);
  }
}
