// 2015 Daniel Bittman <danielbittman1@gmail.com>: http://dbittman.github.io/
#include "rbase.h"
#include "mpscq.h"

#define MPSCQ_MALLOC 1 // flag

void MPSCQueueInit(MPSCQueue* q, size_t cap) {
  q->count = ATOMIC_VAR_INIT(0);
  q->head = ATOMIC_VAR_INIT(0);
  q->tail = 0;
  q->buffer = calloc(cap, sizeof(void*));
  q->max = cap;
  atomic_thread_fence(memory_order_release);
}

void MPSCQueueFree(MPSCQueue* q) {
  free(q->buffer);
  #ifdef DEBUG
  q->buffer = NULL;
  #endif
}

bool MPSCQueueEnqueue(MPSCQueue* q, void *obj) {
  size_t count = atomic_fetch_add_explicit(&q->count, 1, memory_order_acquire);
  if(count >= q->max) {
    // queue is full
    atomic_fetch_sub_explicit(&q->count, 1, memory_order_release);
    return false;
  }

  // increment the head, which gives us 'exclusive' access to that element
  size_t head = atomic_fetch_add_explicit(&q->head, 1, memory_order_acquire);
  assert(q->buffer[head % q->max] == 0);
  void *rv = atomic_exchange_explicit(&q->buffer[head % q->max], obj, memory_order_release);
  assert(rv == NULL);
  return true;
}

void* MPSCQueueDequeue(MPSCQueue* q) {
  void *ret = atomic_exchange_explicit(&q->buffer[q->tail], NULL, memory_order_acquire);
  if(!ret) {
    // a thread is adding to the queue, but hasn't done the atomic_exchange yet
    // to actually put the item in. Act as if nothing is in the queue.
    // Worst case, other producers write content to tail + 1..n and finish, but
    // the producer that writes to tail doesn't do it in time, and we get here.
    // But that's okay, because once it DOES finish, we can get at all the data
    // that has been filled in.
    return NULL;
  }
  if(++q->tail >= q->max)
    q->tail = 0;
  size_t r = atomic_fetch_sub_explicit(&q->count, 1, memory_order_release);
  assert(r > 0);
  return ret;
}

bool MPSCQueueIsEmpty(MPSCQueue* q) {
  void* tail = atomic_load_explicit(&q->buffer[q->tail], memory_order_acquire);
  return tail == NULL;
  // return MPSCQueueLen(q) == 0;
}

size_t MPSCQueueLen(MPSCQueue* q) {
  return atomic_load_explicit(&q->count, memory_order_relaxed);
}
