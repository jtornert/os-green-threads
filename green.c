#include "green.h"

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <ucontext.h>

#define STACK_SIZE 4096
#define PERIOD 100

#define CALL printf("\tcalling %s...\n", __func__)
#define LOG printf("%s\t", __func__);
#define END printf("\tend %s\n", __func__)

static ucontext_t main_cntx = {0};
static green_t main_green = {&main_cntx, NULL, NULL, NULL, NULL, NULL, false};

static green_t *running = &main_green;
static green_t *rq = NULL;

static sigset_t block;

static void init() __attribute__((constructor));
// static void rq_enqueue(rq_t **h, rq_t **t, green_t *thread);
// static green_t *rq_dequeue(rq_t **h, rq_t **t);
// void timer_handler(int sig);

void init() {
  getcontext(&main_cntx);

  // sigemptyset(&block);
  // sigaddset(&block, SIGVTALRM);

  // struct sigaction act = {0};
  // struct timeval interval;
  // struct itimerval period;

  // act.sa_handler = timer_handler;
  // assert(sigaction(SIGVTALRM, &act, NULL) == 0);
  // interval.tv_sec = 0;
  // interval.tv_usec = PERIOD;
  // period.it_interval = interval;
  // period.it_value = interval;
  // setitimer(ITIMER_VIRTUAL, &period, NULL);
}

// void timer_handler(int sig) {
// write(1, "INT\n", 4);
//   green_t *susp = running;
//   rq_enqueue(&rq, &tail, susp);
//   green_t *next;
//   if (susp->next) {
//     next = susp->next;
//   } else {
//     next = rq_dequeue(&rq, &tail);
//   }
//   running = next;
//   swapcontext(susp->context, next->context);
// }

void enqueue(green_t *thread) {
  if (rq) {
    green_t *it = rq;
    while (it->next != NULL) {
      it = it->next;
    }
    it->next = thread;
  } else {
    rq = thread;
  }
}

green_t *dequeue() {
  green_t *thread = rq;
  rq = rq->next;
  thread->next = NULL;
  return thread;
}

void green_thread() {
  green_t *this = running;

  void *result = (*this->fun)(this->arg);
  // place waiting (joining) thread in ready queue
  enqueue(this->join);
  // save result of execution
  this->retval = result;
  // we're a zombie
  this->zombie = true;
  // find the next thread to run
  green_t *next = dequeue();
  running = next;
  setcontext(next->context);
}

int green_yield() {
  // sigprocmask(SIG_BLOCK, &block, NULL);
  green_t *susp = running;
  // add susp to ready queue
  enqueue(susp);
  // select the next thread for execution
  green_t *next = dequeue();
  running = next;
  swapcontext(susp->context, next->context);
  // sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_join(green_t *thread, void **res) {
  // sigprocmask(SIG_BLOCK, &block, NULL);
  if (!thread->zombie) {
    green_t *susp = running;
    // add as joining thread
    thread->join = susp;
    // select the next thread for execution
    green_t *next = dequeue();
    running = next;
    swapcontext(susp->context, next->context);
  }
  // collect result
  if (res) {
    *res = thread->retval;
  }
  // free context
  free(thread->context);
  // sigprocmask(SIG_UNBLOCK, &block, NULL);

  return 0;
}

int green_create(green_t *new, void *(*fun)(void *), void *arg) {
  // // sigprocmask(SIG_BLOCK, &block, NULL);
  ucontext_t *cntx = (ucontext_t *)malloc(sizeof(ucontext_t));
  getcontext(cntx);

  void *stack = malloc(STACK_SIZE);

  cntx->uc_stack.ss_sp = stack;
  cntx->uc_stack.ss_size = STACK_SIZE;
  makecontext(cntx, green_thread, 0);

  new->context = cntx;
  new->fun = fun;
  new->arg = arg;
  new->next = NULL;
  new->join = NULL;
  new->retval = NULL;
  new->zombie = false;

  // add new to the ready queue
  enqueue(new);

  // // sigprocmask(SIG_UNBLOCK, &block, NULL);

  return 0;
}

// void green_cond_init(green_cond_t *cond) {
//   // initialize condition variable
//   // CALL;
//   cond->threads = NULL;
//   cond->next = NULL;
//   // LOG printf("\tcond->threads = %p\tcond->next = %p\n", cond->threads,
//   //  cond->next);
//   // END;
// }

// void green_cond_signal(green_cond_t *cond) {
//   // sigprocmask(SIG_BLOCK, &block, NULL);
//   // signal the condition variable to wake up other threads
//   // CALL;
//   // LOG printf("size: %d\n", cond->size);
//   if (cond->next != NULL) {
//     // LOG printf("waiting threads: %p\n", cond->threads);
//     green_t *thread = rq_dequeue(&(cond->threads), &(cond->next));
//     green_t *susp = running;
//     green_t *next;
//     if (susp->next) {
//       next = susp->next;
//     } else {
//       // LOG printf("switching to waiting thread...\n");
//       next = thread;
//       rq_enqueue(&rq, &tail, susp);
//     }
//     running = next;
//     // END;
//     swapcontext(susp->context, next->context);
//   } else {
//     // LOG printf("no waiting threads - continuing processing running
//     // thread\n");
//     // END;
//   }
//   // sigprocmask(SIG_UNBLOCK, &block, NULL);

// }

// int green_cond_wait(green_cond_t *cond, green_mutex_t *mutex) {
//   // block timer interrupt
//   // sigprocmask(SIG_BLOCK, &block, NULL);
//   // suspend the running thread on condition
//   green_t *susp = running;
//   rq_enqueue(&(cond->threads), &(cond->next), susp);
//   if (mutex != NULL) {
//     // release the lock if we have a mutex
//     green_mutex_unlock(mutex);
//     // move suspended thread to ready queue
//     rq_enqueue(&rq, &tail, susp);
//   }
//   // schedule the next thread
//   green_t *next;
//   if (susp->next) {
//     next = susp->next;
//   } else {
//     next = rq_dequeue(&rq, &tail);
//   }
//   running = next;
//   swapcontext(susp->context, next->context);

//   if (mutex != NULL) {
//     // try to take the lock
//     if (mutex->taken) {
//       // bad luck, suspend
//       green_t *susp = running;
//       rq_enqueue(&(mutex->threads), &(mutex->next), susp);
//       // green_t *next;
//       // if (susp->next) {
//       //   next = susp->next;
//       // } else {
//       //   next = rq_dequeue(&rq, &tail);
//       // }
//       // running = next;
//       // swapcontext(susp->context, next->context);
//     } else {
//       // take the lock
//       mutex->taken = true;
//     }
//   }
//   // unblock
//   // sigprocmask(SIG_UNBLOCK, &block, NULL);

//   return 0;
// }

// int green_mutex_init(green_mutex_t *mutex) {
//   mutex->taken = false;
//   mutex->threads = NULL;
//   mutex->next = NULL;
// }

// int green_mutex_lock(green_mutex_t *mutex) {
//   // block timer interrupt
//   // sigprocmask(SIG_BLOCK, &block, NULL);
//   // CALL;
//   if (mutex->taken) {
//     green_t *susp = running;
//     // suspend the running thread
//     rq_enqueue(&(mutex->threads), &(mutex->next), susp);
//     // find the next thread
//     green_t *next;
//     if (susp->next) {
//       next = susp->next;
//     } else {
//       next = rq_dequeue(&rq, &tail);
//     }
//     running = next;
//     // LOG printf("running next thread...\n");
//     swapcontext(susp->context, next->context);
//   } else {
//     mutex->taken = true;
//   }
//   // END;
//   // sigprocmask(SIG_UNBLOCK, &block, NULL);

//   return 0;
// }

// int green_mutex_unlock(green_mutex_t *mutex) {
//   // block timer interrupt
//   // sigprocmask(SIG_BLOCK, &block, NULL);
//   if (mutex->next != NULL) {
//     // move suspended thread to ready queue
//     green_t *susp = rq_dequeue(&(mutex->threads), &(mutex->next));
//     rq_enqueue(&rq, &tail, susp);
//   } else {
//     // release lock
//     mutex->taken = false;
//   }
//   // unblock
//   // sigprocmask(SIG_UNBLOCK, &block, NULL);

//   return 0;
// }

// int main(int argc, char const *argv[]) {
//   // sigprocmask(SIG_BLOCK, &block, NULL);
//   green_t t0, t1, t2, t3, t4;
//   t0.next = NULL;
//   t1.next = NULL;
//   t2.next = NULL;
//   t3.next = NULL;
//   t4.next = NULL;

//   enqueue(&t0);
//   enqueue(&t1);
//   enqueue(&t2);
//   enqueue(&t3);

//   LOG printf("\tgot here: enqueued 4 elements\n");

//   dequeue();
//   dequeue();
//   dequeue();
//   dequeue();

//   LOG printf("\tgot here: dequeued 4 elements\n");

//   enqueue(&t4);

//   LOG printf("\tgot here: enqueued 1 element\n");

//   dequeue();

//   LOG printf("\tgot here: dequeued 1 element\n");

//   // sigprocmask(SIG_UNBLOCK, &block, NULL);

//   return 0;
// }
