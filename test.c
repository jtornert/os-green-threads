#include <stdio.h>
#include <string.h>

#include "green.h"

#define thread(x) void *x(void *arg)
#define get(x) *(x *)arg

// yield
void *test1(void *arg) {
  int i = *(int *)arg;
  int loop = 4;
  while (loop > 0) {
    printf("thread %d: %d\n", i, loop);
    loop--;
    green_yield();
  }
}

int flag = 0;
green_cond_t cond;

// condition variables
void *test2(void *arg) {
  int id = *(int *)arg;
  int loop = 4;
  while (loop > 0) {
    if (flag == id) {
      printf("thread %d: %d\n", id, loop);
      loop--;
      flag = (id + 1) % 2;
      green_cond_signal(&cond);
    } else {
      green_cond_wait(&cond, NULL);
    }
  }
}

int x = 0;

// timer interrupt
thread(test3) {
  int id = get(int);
  for (size_t i = 0; i < 1000000000; i++) {
    x++;
  }
}

green_mutex_t mutex;

// timer interrupt with mutex
thread(test4) {
  int id = get(int);
  for (size_t i = 0; i < 10000; i++) {
    green_mutex_lock(&mutex);
    x++;
    green_mutex_unlock(&mutex);
  }
}

// timer interrupt with mutex and condition variable
thread(test5) {
  int id = get(int);
  int loop = 4;
  while (loop > 0) {
    green_mutex_lock(&mutex);
    while (flag != id) {
      green_cond_wait(&cond, &mutex);
      green_mutex_lock(&mutex);
    }
    flag = (id + 1) % 2;
    green_cond_signal(&cond);
    green_mutex_unlock(&mutex);
    loop--;
  }
}

int main(int argc, char const *argv[]) {
  green_cond_init(&cond);
  green_mutex_init(&mutex);
  green_t g0, g1;
  int a0 = 0;
  int a1 = 1;
  green_create(&g0, test1, &a0);
  green_create(&g1, test1, &a1);

  green_join(&g0, NULL);
  green_join(&g1, NULL);
  printf("x: %d\n", x);
  printf("done\n");
  return 0;
}
