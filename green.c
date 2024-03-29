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
static sigset_t block;
static rq_t *rq = NULL;
static rq_t *tail = NULL;

static void init() __attribute__((constructor));
static void rq_enqueue(rq_t **h, rq_t **t, green_t *thread);
static green_t *rq_dequeue(rq_t **h, rq_t **t);
void timer_handler(int sig);

void init() {
  getcontext(&main_cntx);

  sigemptyset(&block);
  sigaddset(&block, SIGVTALRM);

  struct sigaction act = {0};
  struct timeval interval;
  struct itimerval period;

  act.sa_handler = timer_handler;
  assert(sigaction(SIGVTALRM, &act, NULL) == 0);
  interval.tv_sec = 0;
  interval.tv_usec = PERIOD;
  period.it_interval = interval;
  period.it_value = interval;
  setitimer(ITIMER_VIRTUAL, &period, NULL);
}

void timer_handler(int sig) {
  write(1, "INT\n", 4);
  green_t *susp = running;
  rq_enqueue(&rq, &tail, susp);
  green_t *next;
  if (susp->next) {
    next = susp->next;
  } else {
    next = rq_dequeue(&rq, &tail);
  }
  running = next;
  swapcontext(susp->context, next->context);
}

void rq_enqueue(rq_t **h, rq_t **t, green_t *thread) {
  rq_t *q = malloc(sizeof(rq_t));
  q->thread = thread;
  q->next = *h;
  q->prev = NULL;

  if (*h == NULL) {
    *t = q;
  } else {
    (*h)->prev = q;
  }

  *h = q;
}

green_t *rq_dequeue(rq_t **h, rq_t **t) {
  rq_t *q = *t;
  if (q->prev) {
    q->prev->next = NULL;
  } else {
    *h = NULL;
  }
  green_t *thread = (*t)->thread;
  *t = q->prev;
  free(q);
  return thread;
}

void green_thread() {
  // printf("%s:\tstart\n", __func__);
  green_t *this = running;
  void *result = (*this->fun)(this->arg);
  // place waiting thread in ready queue
  if (this->join) {
    rq_enqueue(&rq, &tail, this->join);
  }
  // save result of execution
  // printf("%s:\tresult = %p\n", __func__, result);
  this->retval = result;
  // we're a zombie
  this->zombie = true;
  // find the next thread to run
  green_t *next;
  if (this->next) {
    next = this->next;
  } else {
    next = rq_dequeue(&rq, &tail);
  }
  running = next;
  // printf("%s:\tend\n", __func__);
  setcontext(next->context);
}

int green_yield() {
  sigprocmask(SIG_BLOCK, &block, NULL);
  // printf("%s:\tstart\n", __func__);
  green_t *susp = running;
  // add susp last in the ready queue
  rq_enqueue(&rq, &tail, susp);
  // select the next thread for execution
  green_t *next;
  if (susp->next) {
    next = susp->next;
  } else {
    next = rq_dequeue(&rq, &tail);
  }
  running = next;
  swapcontext(susp->context, next->context);
  // printf("%s:\trunning = %p\n", __func__, running);
  // printf("%s:\tend\n", __func__);
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_join(green_t *thread, void **res) {
  // printf("%s:\tstart\n", __func__);
  sigprocmask(SIG_BLOCK, &block, NULL);
  if (!thread->zombie) {
    green_t *susp = running;
    // add as joining thread
    thread->join = running;
    // select the next thread for execution
    green_t *next;
    if (thread->next) {
      next = thread->next;
    } else {
      next = rq_dequeue(&rq, &tail);
    }
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
  // printf("%s:\tend\n", __func__);
  return 0;
}

int green_create(green_t *new, void *(*fun)(void *), void *arg) {
  sigprocmask(SIG_BLOCK, &block, NULL);
  // printf("%s:\tstart\n", __func__);
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

  rq_enqueue(&rq, &tail, new);

  sigprocmask(SIG_UNBLOCK, &block, NULL);
  // printf("%s:\tend\n", __func__);
  return 0;
}

void green_cond_init(green_cond_t *cond) {
  // initialize condition variable
  // CALL;
  cond->threads = NULL;
  cond->next = NULL;
  // LOG printf("\tcond->threads = %p\tcond->next = %p\n", cond->threads,
  //  cond->next);
  // END;
}

void green_cond_signal(green_cond_t *cond) {
  sigprocmask(SIG_BLOCK, &block, NULL);
  // signal the condition variable to wake up other threads
  // CALL;
  // LOG printf("size: %d\n", cond->size);
  if (cond->next != NULL) {
    // LOG printf("waiting threads: %p\n", cond->threads);
    green_t *thread = rq_dequeue(&(cond->threads), &(cond->next));
    green_t *susp = running;
    green_t *next;
    if (susp->next) {
      next = susp->next;
    } else {
      // LOG printf("switching to waiting thread...\n");
      next = thread;
      rq_enqueue(&rq, &tail, susp);
    }
    running = next;
    // END;
    swapcontext(susp->context, next->context);
  } else {
    // LOG printf("no waiting threads - continuing processing running
    // thread\n");
    // END;
  }
  sigprocmask(SIG_UNBLOCK, &block, NULL);
}

int green_cond_wait(green_cond_t *cond, green_mutex_t *mutex) {
  // block timer interrupt
  sigprocmask(SIG_BLOCK, &block, NULL);
  // suspend the running thread on condition
  green_t *susp = running;
  rq_enqueue(&(cond->threads), &(cond->next), susp);
  if (mutex != NULL) {
    // release the lock if we have a mutex
    green_mutex_unlock(mutex);
    // move suspended thread to ready queue
    rq_enqueue(&rq, &tail, susp);
  }
  // schedule the next thread
  green_t *next;
  if (susp->next) {
    next = susp->next;
  } else {
    next = rq_dequeue(&rq, &tail);
  }
  running = next;
  swapcontext(susp->context, next->context);

  if (mutex != NULL) {
    // try to take the lock
    if (mutex->taken) {
      // bad luck, suspend
      green_t *susp = running;
      rq_enqueue(&(mutex->threads), &(mutex->next), susp);
      // green_t *next;
      // if (susp->next) {
      //   next = susp->next;
      // } else {
      //   next = rq_dequeue(&rq, &tail);
      // }
      // running = next;
      // swapcontext(susp->context, next->context);
    } else {
      // take the lock
      mutex->taken = true;
    }
  }
  // unblock
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_mutex_init(green_mutex_t *mutex) {
  mutex->taken = false;
  mutex->threads = NULL;
  mutex->next = NULL;
}

int green_mutex_lock(green_mutex_t *mutex) {
  // block timer interrupt
  sigprocmask(SIG_BLOCK, &block, NULL);
  // CALL;
  if (mutex->taken) {
    green_t *susp = running;
    // suspend the running thread
    rq_enqueue(&(mutex->threads), &(mutex->next), susp);
    // find the next thread
    green_t *next;
    if (susp->next) {
      next = susp->next;
    } else {
      next = rq_dequeue(&rq, &tail);
    }
    running = next;
    // LOG printf("running next thread...\n");
    swapcontext(susp->context, next->context);
  } else {
    mutex->taken = true;
  }
  // END;
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

int green_mutex_unlock(green_mutex_t *mutex) {
  // block timer interrupt
  sigprocmask(SIG_BLOCK, &block, NULL);
  if (mutex->next != NULL) {
    // move suspended thread to ready queue
    green_t *susp = rq_dequeue(&(mutex->threads), &(mutex->next));
    rq_enqueue(&rq, &tail, susp);
  } else {
    // release lock
    mutex->taken = false;
  }
  // unblock
  sigprocmask(SIG_UNBLOCK, &block, NULL);
  return 0;
}

// int main(int argc, char const *argv[]) {
//   sigprocmask(SIG_BLOCK, &block, NULL);

//   rq_t *h = NULL;
//   rq_t *t = NULL;

//   rq_enqueue(&h, &t, (green_t *)1);
//   rq_enqueue(&h, &t, (green_t *)2);
//   rq_enqueue(&h, &t, (green_t *)3);
//   rq_enqueue(&h, &t, (green_t *)4);

//   printf("got here: enqueued 4 elements\n");

//   rq_dequeue(&h, &t);
//   rq_dequeue(&h, &t);
//   rq_dequeue(&h, &t);
//   rq_dequeue(&h, &t);

//   printf("got here: dequeued 4 elements\n");

//   rq_enqueue(&h, &t, (green_t *)5);

//   printf("got here: enqueued 1 element\n");

//   rq_dequeue(&h, &t);

//   printf("got here: dequeued 1 element\n");

//   sigprocmask(SIG_UNBLOCK, &block, NULL);
//   return 0;
// }
