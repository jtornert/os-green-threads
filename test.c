#include <stdio.h>
#include <string.h>

#include "green.h"

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
      green_cond_wait(&cond);
    }
  }
}

size_t x = 0;

// timer interrupt
void *test3(void *arg) {
  int id = *(int *)arg;
  for (size_t i = 0; i < 100000000; i++) {
    x++;
  }
}

void *test4(void *arg) {
  int id = *(int *)arg;
  int loop = 4;
  char buffer[256];
  while (loop > 0) {
    if (flag == id) {
      // sprintf(buffer, "thread %d: %d\n", id, loop);
      // int length = strlen(buffer);
      // write(1, buffer, length);
      loop--;
      flag = (id + 1) % 2;
      green_cond_signal(&cond);
    } else {
      green_cond_wait(&cond);
    }
  }
}

green_mutex_t mutex;

// timer interrupt with mutex
void *test5(void *arg) {
  int id = *(int *)arg;
  int failed;
  for (size_t i = 0; i < 100000; i++) {
    green_mutex_lock(&mutex);
    x++;
    green_mutex_unlock(&mutex);
  }
}

// // timer interrupt with mutex and condition variable
// void *test5(void *arg) {
//   int id = *(int *)arg;
//   int loop = 4;
//   while (loop > 0) {
//     green_mutex_lock(&mutex);
//     while (flag != id) {
//       green_cond_wait(&cond, &mutex);
//       green_mutex_lock(&mutex);
//     }
//     flag = (id + 1) % 2;
//     green_cond_signal(&cond);
//     green_mutex_unlock(&mutex);
//     loop--;
//   }
// }

int main(int argc, char const *argv[]) {
  green_cond_init(&cond);
  green_mutex_init(&mutex);
  green_t g0, g1;
  int a0 = 0;
  int a1 = 1;
  green_create(&g0, test5, &a0);
  green_create(&g1, test5, &a1);

  green_join(&g0, NULL);
  green_join(&g1, NULL);
  printf("x: %ld\n", x);
  printf("done\n");
  return 0;
}
