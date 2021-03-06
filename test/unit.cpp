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

/* This is a unit test of deadlock prevention. This is based on the Dining
 * Philosophers Problem (http://en.wikipedia.org/wiki/Dining_philosophers_
 * problem). This obviously needs to be documented better.
 */

#include <vector>
#include <memory>

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>

#include "locking-container.hpp"
//(necessary for non-template source)
#include "locking-container.inc"

#define SUCCESS        0
#define ERROR_ARGS     1
#define ERROR_THREAD   2
#define ERROR_DEADLOCK 3
#define ERROR_LOGIC    4
#define ERROR_SYSTEM   5


//typedefs

struct chopstick;
typedef lc::locking_container_base <chopstick> protected_chopstick;
typedef std::shared_ptr <protected_chopstick>  chopstick_pointer;
typedef std::vector <chopstick_pointer>        chopstick_set;

struct philosopher_base;
typedef std::unique_ptr <philosopher_base> philosopher_pointer;
typedef std::vector <philosopher_pointer>  philosopher_set;

typedef std::vector <pthread_t> thread_set;


//chopsticks for use by philosophers

struct chopstick {
  chopstick() : value(-1), retries(0) {}
  int value, retries;
};


//philosophers, who must grab the left chopstick before the right

struct philosopher_base {
  //TODO: add a condition where a write lock is used for "right"; this will be
  //necessary in order to test the lock-out exceptions used in 'rw_lock'

  virtual lc::meta_lock::write_proxy       lock_multi() = 0;
  virtual protected_chopstick::write_proxy write_left() = 0;
  virtual protected_chopstick::read_proxy  read_right() = 0;

  virtual int get_number()      const = 0;
  virtual int get_left_order()  const = 0;
  virtual int get_right_order() const = 0;

  virtual bool barrier_wait()                    = 0;
  virtual void timed_wait(bool optional = false) = 0;

  virtual inline ~philosopher_base() {}

  static void *eat_dinner(philosopher_base *self) {
    //TODO: error message
    if (!self || !self->barrier_wait()) exit(ERROR_THREAD);

    for (int retries = 0; true; retries++) {
      //NOTE: this allows everything to remain unlocked briefly, which is what
      //stops an infinite loop for auth.-based deadlock prevention
      //NOTE: skipping a wait every once in a while allows the threads to get
      //out of sync, in case that's causing a de facto deadlock
      if (retries > 0 && (retries + self->get_number() + 1) % 2) self->timed_wait();

      //NOTE: this should always succeed if multilocking is used; the return
      //value isn't important, because a 'NULL' should mean that we're not
      //using multilocking
      lc::meta_lock::write_proxy multi = self->lock_multi();
      bool using_multi = multi;

      //NOTE: this should only fail if there's an incompatibility between the
      //lock type, locking method, or auth. type, but that should be prevented
      //during argument parsing
      protected_chopstick::write_proxy left = self->write_left();
      if (!left) exit(ERROR_LOGIC);

      //(increase the chances of a potential deadlock)
      //NOTE: 'true' allows the object to skip the wait (this is the difference
      //between the two try-to-deadlock command-line options)
      self->timed_wait(true);

      //NOTE: this will fail if a potential deadlock is detected
      protected_chopstick::read_proxy right = self->read_right();
      multi.clear(); //(clear the multi-lock as soon as possible)
      if (!right) {
        //(if method 2 is used, the above lock should never fail)
        if (using_multi) exit(ERROR_LOGIC);
        //NOTE: if you 'timed_wait' here, 'left' remains locked during the wait!
        continue;
      } else {
        //(if 'right' was already used, pass on its number)
        left->value = (right->value < 0)? self->get_number() : right->value;
        left->retries = retries;
        fprintf(stdout, "thread:\t%i\t%i\t%i\n", self->get_number(), left->value, left->retries);
        break;
      }
    }

    //TODO: error message
    if (!self->barrier_wait()) exit(ERROR_THREAD);
    return NULL;
  }
};


//a sentient philosopher, with an actual strategy

class philosopher : public philosopher_base {
public:
  philosopher(int n, chopstick_pointer l, chopstick_pointer r, pthread_barrier_t *b,
    lc::lock_auth_base::auth_type a = lc::lock_auth_base::auth_type(),
    lc::shared_meta_lock m = lc::shared_meta_lock(), bool d = true) :
    number(n), deadlock(d), barrier(b), auth(a), multi(m), left(l), right(r) {
    assert(left.get() && right.get());
  }

  lc::meta_lock::write_proxy lock_multi() {
    //(method 2 : methods 0, 1, & 3)
    return multi? multi->get_write_auth(auth) : lc::meta_lock::write_proxy();
  }

  protected_chopstick::write_proxy write_left() {
    //NOTE: if the auth. type will always reject the lock, might as well exit
    //TODO: error message
    if (auth && !auth->guess_write_allowed(false, false, left->get_order())) exit(ERROR_LOGIC);
    if (auth && multi && !auth->guess_read_allowed())                        exit(ERROR_LOGIC);
    //(method 2)
    if (multi) return left->get_write_multi(*multi, auth);
    //(methods 1 & 3)
    if (auth)  return left->get_write_auth(auth);
    //(method 0)
    return left->get_write();
  }

  protected_chopstick::read_proxy read_right() {
    //NOTE: if the auth. type will always reject the lock, might as well exit
    //TODO: error message
    if (auth && !auth->guess_read_allowed(false, false, right->get_order())) exit(ERROR_LOGIC);
    if (auth && multi && !auth->guess_read_allowed())                        exit(ERROR_LOGIC);
    //(method 2)
    if (multi) return right->get_read_multi(*multi, auth);
    //(methods 1 & 3)
    if (auth)  return right->get_read_auth(auth);
    //(method 0)
    return right->get_read();
  }

  int get_number() const {
    return number;
  }

  int get_left_order() const {
    return left->get_order();
  }

  int get_right_order() const {
    return right->get_order();
  }

  bool barrier_wait() {
    int result = pthread_barrier_wait(barrier);
    return result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD;
  }

  void timed_wait(bool optional) {
    if (optional && !deadlock) return;
    struct timespec wait = { 0, 10 * 1000 * 1000 };
    nanosleep(&wait, NULL);
  }

protected:
  const int                     number;
  bool                          deadlock;
  pthread_barrier_t            *barrier;
  lc::lock_auth_base::auth_type auth;
  lc::shared_meta_lock          multi;
  chopstick_pointer             left, right;
};


//helper functions

static int print_help(const char *name, const char *message = NULL);

static void deadlock_timeout(int sig);

static void init_chopsticks(int lock_method, int lock_type, chopstick_set &chops);

static void init_philosophers(int lock_method, int auth_type, chopstick_set &chops,
  philosopher_set &phils, pthread_barrier_t *barrier, lc::shared_meta_lock multi,
  bool deadlock);

static void start_threads(thread_set &threads, philosopher_set &phils,
  pthread_barrier_t *barrier, int timeout);

static void get_results(thread_set &threads, chopstick_set &chops, pthread_barrier_t *barrier);


//the program proper

int main(int argc, char *argv[]) {
  char error = 0;
  int thread_count = 0, lock_method = 0, try_deadlock = 1, lock_type = 0, auth_type = 0, timeout = 1;

  //argument parsing

  if (argc != 6 && argc != 7) return print_help(argv[0]);

  if (sscanf(argv[1], "%i%c", &thread_count, &error) != 1 || thread_count < 2 || thread_count > 256)
    return print_help(argv[0], "invalid number of threads");

  if (sscanf(argv[2], "%i%c", &lock_method, &error) != 1 || lock_method < 0 || lock_method > 4)
    return print_help(argv[0], "invalid lock method");

  if (sscanf(argv[3], "%i%c", &try_deadlock, &error) != 1 || try_deadlock < 0 || try_deadlock > 1)
    return print_help(argv[0], "invalid deadlock value");

  if (sscanf(argv[4], "%i%c", &lock_type, &error) != 1 || lock_type < 0 || lock_type > 3)
    return print_help(argv[0], "invalid lock type");

  if (sscanf(argv[5], "%i%c", &auth_type, &error) != 1 || auth_type < 0 || auth_type > 4)
    return print_help(argv[0], "invalid auth type");

  if (argc > 6 && (sscanf(argv[6], "%i%c", &timeout, &error) != 1 || timeout < 1))
    return print_help(argv[0], "invalid timeout value");

  if (lock_method == 0 && auth_type != 0)
    return print_help(argv[0], "auth type must be 0 with unsafe locking");

  if (lock_method == 3 && auth_type < 2)
    return print_help(argv[0], "auth type must be >= 2 with ordered locks");

  if (lock_method == 2 && try_deadlock)
    return print_help(argv[0], "cannot cause a deadlock with multi-locking");

  //program data

  philosopher_set   all_philosophers(thread_count);
  chopstick_set     all_chopsticks(thread_count);
  lc::shared_meta_lock multi((lock_method == 2)? new lc::meta_lock : NULL);
  thread_set        all_threads(thread_count);
  pthread_barrier_t barrier;
  struct timespec   start, finish;

  //initialization

  signal(SIGALRM, &deadlock_timeout);

  //TODO: error message
  if (pthread_barrier_init(&barrier, NULL, thread_count + 1) != 0) return ERROR_SYSTEM;

  //initialize chopsticks first
  init_chopsticks(lock_method, lock_type, all_chopsticks);

  //initialize philosophers second
  init_philosophers(lock_method, auth_type, all_chopsticks, all_philosophers,
    &barrier, multi, try_deadlock);

  //program execution

  clock_gettime(CLOCK_MONOTONIC, &start);

  //start the threads last
  start_threads(all_threads, all_philosophers, &barrier, timeout);

  //wait for results
  get_results(all_threads, all_chopsticks, &barrier);

  clock_gettime(CLOCK_MONOTONIC, &finish);

  fprintf(stdout, "time: %f\n",
    (double) (finish.tv_sec  - start.tv_sec)  +
    (double) (finish.tv_nsec - start.tv_nsec) / (double) (1000. * 1000. * 1000.));

  //cleanup

  pthread_barrier_destroy(&barrier);

  return SUCCESS;
}


//helper functions

static int print_help(const char *name, const char *message) {
  if (message) fprintf(stderr, "%s: %s\n", name, message);
  fprintf(stderr, "%s [threads] [lock method] [deadlock?] [lock type] [auth type] (timeout)\n", name);
  fprintf(stderr, "[threads]: number of threads to run (2-256)\n");
  fprintf(stderr, "[lock method]: container locking method to use\n");
  fprintf(stderr, "  0: unsafe (no deadlock prevention)\n");
  fprintf(stderr, "  1: authorization only\n");
  fprintf(stderr, "  2: multi-locking\n");
  fprintf(stderr, "  3: ordered locking\n");
  fprintf(stderr, "[deadlock?]: attempt to cause a deadlock?\n");
  fprintf(stderr, "  0: no\n");
  fprintf(stderr, "  1: yes\n");
  fprintf(stderr, "[lock type]: type of container locks to use\n");
  fprintf(stderr, "  0: rw_lock\n");
  fprintf(stderr, "  1: w_lock\n");
  fprintf(stderr, "  2: dumb_lock\n");
  fprintf(stderr, "[auth type]: type of authorization objects to use\n");
  fprintf(stderr, "  0: rw_lock\n");
  fprintf(stderr, "  1: w_lock\n");
  fprintf(stderr, "  2: ordered_lock <rw_lock>\n");
  fprintf(stderr, "  3: ordered_lock <w_lock>\n");
  fprintf(stderr, "(timeout): time (in seconds) to wait for deadlock (default: 1s)\n");
  return ERROR_ARGS;
}


static void deadlock_timeout(int sig) {
  fprintf(stdout, "(deadlock timeout)\n");
  exit(ERROR_DEADLOCK);
}


static void init_chopsticks(int lock_method, int lock_type, chopstick_set &chops) {
  for (int i = 0; i < (signed) chops.size(); i++) {
    switch (lock_method) {
      case 0:
      case 1:
      case 2:
        switch (lock_type) {
          case 0: chops[i].reset(new lc::locking_container <chopstick, lc::rw_lock>);   break;
          case 1: chops[i].reset(new lc::locking_container <chopstick, lc::w_lock>);    break;
          case 2: chops[i].reset(new lc::locking_container <chopstick, lc::dumb_lock>); break;
          default: exit(ERROR_ARGS); break;
        }
        break;
      case 3:
        switch (lock_type) {
          //NOTE: lock order must be > 0 for order rules to apply
          case 0: chops[i].reset(new lc::locking_container <chopstick, lc::ordered_lock <lc::rw_lock> >   (chopstick(), i + 1)); break;
          case 1: chops[i].reset(new lc::locking_container <chopstick, lc::ordered_lock <lc::w_lock> >    (chopstick(), i + 1)); break;
          case 2: chops[i].reset(new lc::locking_container <chopstick, lc::ordered_lock <lc::dumb_lock> > (chopstick(), i + 1)); break;
          default: exit(ERROR_ARGS); break;
        }
        break;
      default: exit(ERROR_ARGS); break;
    }
    if (!chops[i]) exit(ERROR_SYSTEM);
  }
}


static void init_philosophers(int lock_method, int auth_type, chopstick_set &chops,
  philosopher_set &phils, pthread_barrier_t *barrier, lc::shared_meta_lock multi,
  bool deadlock) {
  for (int i = 0; i < (signed) phils.size(); i++) {
    lc::lock_auth_base::auth_type new_auth;
    switch (lock_method) {
      case 0: break; //(no auth. used)
      case 1:
      case 2:
        switch (auth_type) {
          case 0: new_auth.reset(new lc::lock_auth <lc::rw_lock>); break;
          case 1: new_auth.reset(new lc::lock_auth <lc::w_lock>);  break;
          case 2: new_auth.reset(new lc::lock_auth <lc::ordered_lock <lc::rw_lock> >); break;
          case 3: new_auth.reset(new lc::lock_auth <lc::ordered_lock <lc::w_lock> >);  break;
          default: exit(ERROR_ARGS); break;
        }
        break;
      case 3:
        switch (auth_type) {
          case 0:
          case 1: exit(ERROR_ARGS); break;
          case 2: new_auth.reset(new lc::lock_auth <lc::ordered_lock <lc::rw_lock> >); break;
          case 3: new_auth.reset(new lc::lock_auth <lc::ordered_lock <lc::w_lock> >);  break;
          default: exit(ERROR_ARGS); break;
        }
        break;
      default: exit(ERROR_ARGS); break;
    }
    if ((bool) lock_method ^ (bool) new_auth.get()) exit(ERROR_LOGIC);
    phils[i].reset(new
      philosopher(i, chops[i % chops.size()], chops[(i + 1) % chops.size()],
        barrier, new_auth, multi, deadlock));
    if (!phils[i]) exit(ERROR_SYSTEM);
  }
}


static void start_threads(thread_set &threads, philosopher_set &phils,
  pthread_barrier_t *barrier, int timeout) {
  for (int i = 0; i < (signed) phils.size(); i++) {
    if (pthread_create(&threads[i], NULL, (void*(*)(void*)) &philosopher_base::eat_dinner,
        (void*) static_cast <philosopher_base*> (phils[i].get())) != 0) {
      //TODO: error message
      exit(ERROR_SYSTEM);
    }
  }

  struct itimerval timer = { { 0, 0 }, { timeout, 0 } };
  //TODO: error message
  if (setitimer(ITIMER_REAL, &timer, NULL) != 0) exit(ERROR_SYSTEM);

  int result = pthread_barrier_wait(barrier);
  //TODO: error message
  if (result != 0 && result != PTHREAD_BARRIER_SERIAL_THREAD) exit(ERROR_SYSTEM);
}


static void get_results(thread_set &threads, chopstick_set &chops, pthread_barrier_t *barrier) {
  int result = pthread_barrier_wait(barrier);
  //TODO: error message
  if (result != 0 && result != PTHREAD_BARRIER_SERIAL_THREAD) exit(ERROR_SYSTEM);

  for (int i = 0; i < (signed) threads.size(); i++) {
    pthread_join(threads[i], NULL);
  }

  lc::lock_auth_base::auth_type auth(new lc::lock_auth_max);

  for (int i = 0; i < (signed) chops.size(); i++) {
    protected_chopstick::read_proxy read = chops[i]->get_read_auth(auth);
    //TODO: error message
    if (!read) exit(ERROR_LOGIC);
    fprintf(stdout, "final:\t%i\t%i\t%i\n", i, read->value, read->retries);
  }
}
