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
static void enqueue(green_t **queue, green_t *thread);
static green_t *dequeue(green_t **queue);
static void timer_handler(int sig);

void init() {
  getcontext(&main_cntx);

  sigemptyset(&block);
  sigaddset(&block, SIGALRM);

  struct sigaction act = {0};
  struct timeval interval;
  struct itimerval period;

  act.sa_handler = timer_handler;
  assert(sigaction(SIGALRM, &act, NULL) == 0);
  interval.tv_sec = 0;
  interval.tv_usec = PERIOD;
  period.it_interval = interval;
  period.it_value = interval;
  setitimer(ITIMER_REAL, &period, NULL);
}

void timer_handler(int sig) {
  sigprocmask(SIG_BLOCK, &block, NULL);
  // write(1, "INT\n", 4);
  green_t *susp = running;
  enqueue(&rq, susp);
  green_t *next = dequeue(&rq);
  running = next;
  swapcontext(susp->context, next->context);
  sigprocmask(SIG_UNBLOCK, &block, NULL);
}

void enqueue(green_t **queue, green_t *thread) {
  if (*queue) {
    green_t *it = *queue;
    while (it->next != NULL) {
      it = it->next;
    }
    if (it != thread) {
      it->next = thread;
    }
  } else {
    *queue = thread;
  }
}

green_t *dequeue(green_t **queue) {
  green_t *thread = *queue;
  *queue = (*queue)->next;
  thread->next = NULL;
  return thread;
}

void green_thread() {
  green_t *this = running;

  void *result = (*this->fun)(this->arg);
  // place waiting (joining) thread in ready queue
  sigprocmask(SIG_BLOCK, &block, NULL);
  enqueue(&rq, this->join);
  // save result of execution
  this->retval = result;
  // we're a zombie
  this->zombie = true;
  // find the next thread to run
  green_t *next = dequeue(&rq);
  running = next;
  setcontext(next->context);
  sigprocmask(SIG_UNBLOCK, &block, NULL);
}

int green_yield() {
  sigprocmask(SIG_BLOCK, &block, NULL);
  green_t *susp = running;
  // add susp to ready queue
  enqueue(&rq, susp);
  // select the next thread for execution
  green_t *next = dequeue(&rq);
  // sigprocmask(SIG_UNBLOCK, &block, NULL);
  running = next;
  swapcontext(susp->context, next->context);
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_join(green_t *thread, void **res) {
  sigprocmask(SIG_BLOCK, &block, NULL);
  if (!thread->zombie) {
    green_t *susp = running;
    // add as joining thread
    thread->join = susp;
    // select the next thread for execution
    green_t *next = dequeue(&rq);
    running = next;
    swapcontext(susp->context, next->context);
  }
  // collect result
  if (res) {
    *res = thread->retval;
  }
  // free context
  free(thread->context);
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_create(green_t *new, void *(*fun)(void *), void *arg) {
  // sigprocmask(SIG_BLOCK, &block, NULL);
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
  enqueue(&rq, new);

  // sigprocmask(SIG_UNBLOCK, &block, NULL);

  return 0;
}

void green_cond_init(green_cond_t *cond) {
  sigprocmask(SIG_BLOCK, &block, NULL);
  cond->susp = NULL;
  sigprocmask(SIG_UNBLOCK, &block, NULL);
}

void green_cond_signal(green_cond_t *cond) {
  sigprocmask(SIG_BLOCK, &block, NULL);
  if (cond->susp != NULL) {
    green_t *next = dequeue(&(cond->susp));
    green_t *susp = running;
    enqueue(&rq, susp);
    running = next;
    swapcontext(susp->context, next->context);
  }
  sigprocmask(SIG_UNBLOCK, &block, NULL);
}

int green_cond_wait(green_cond_t *cond, green_mutex_t *mutex) {
  // block timer interrupt
  sigprocmask(SIG_BLOCK, &block, NULL);
  // suspend the running thread on condition
  green_t *susp = running;
  green_t *next;
  if (cond->susp) {
    next = dequeue(&(cond->susp));
  } else {
    enqueue(&(cond->susp), susp);
    next = dequeue(&rq);
  }
  running = next;
  swapcontext(susp->context, next->context);
  // unblock
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_mutex_init(green_mutex_t *mutex) {
  sigprocmask(SIG_BLOCK, &block, NULL);
  mutex->taken = false;
  mutex->susp = NULL;
  sigprocmask(SIG_UNBLOCK, &block, NULL);
}

int green_mutex_lock(green_mutex_t *mutex) {
  // block timer interrupt
  sigprocmask(SIG_BLOCK, &block, NULL);
  if (mutex->taken) {
    // suspend the running thread
    green_t *susp = running;
    enqueue(&(mutex->susp), susp);
    // find the next thread
    green_t *next = dequeue(&rq);
    running = next;
    swapcontext(susp->context, next->context);
  } else {
    // take the lock
    __sync_val_compare_and_swap(&(mutex->taken), false, true);
  }
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_mutex_unlock(green_mutex_t *mutex) {
  // block timer interrupt
  sigprocmask(SIG_BLOCK, &block, NULL);
  if (mutex->susp != NULL) {
    // move suspended thread to ready queue
    green_t *susp = dequeue(&(mutex->susp));
    enqueue(&rq, susp);
  } else {
    // release lock
    mutex->taken = false;
  }
  // unblock
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

// void print() {
//   if (!rq) {
//     printf("[]\n");
//     return;
//   }

//   putchar('[');
//   green_t *it = rq;
//   for (; it->next; it = it->next) {
//     printf("%p, ", it);
//   }
//   printf("%p]\n", it);
// }

// int main(int argc, char const *argv[]) {
//   // sigprocmask(SIG_BLOCK, &block, NULL);
//   green_t t0, t1, t2, t3, t4;
//   t0.next = NULL;
//   t1.next = NULL;
//   t2.next = NULL;
//   t3.next = NULL;
//   t4.next = NULL;

//   enqueue(&rq, &t0);
//   print();
//   enqueue(&rq, &t1);
//   print();
//   enqueue(&rq, &t2);
//   print();
//   enqueue(&rq, &t3);
//   print();

//   LOG printf("\tgot here: enqueued 4 elements\n");

//   dequeue(&rq);
//   print();
//   dequeue(&rq);
//   print();
//   dequeue(&rq);
//   print();
//   dequeue(&rq);
//   print();

//   LOG printf("\tgot here: dequeued 4 elements\n");

//   enqueue(&rq, &t4);
//   print();

//   LOG printf("\tgot here: enqueued 1 element\n");

//   green_t *thread = dequeue(&rq);
//   print();

//   printf("thread->next: %p\n", thread->next);

//   LOG printf("\tgot here: dequeued 1 element\n");

//   // sigprocmask(SIG_UNBLOCK, &block, NULL);

//   return 0;
// }
