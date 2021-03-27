#include "rbase.h"

ThreadStatus ThreadStart(Thread* nonull t, thrd_start_t nonull fn, void* nullable arg) {
  return (ThreadStatus)thrd_create(t, fn, arg);
}

int ThreadAwait(Thread t) {
  int result = 0;
  thrd_join(t, &result); // ignore ThreadStatus
  return result;
}

Thread nullable ThreadSpawn(thrd_start_t nonull fn, void* nullable arg) {
  Thread t;
  if (ThreadStart(&t, fn, arg) != ThreadSuccess) {
    return NULL;
  }
  return t;
}
