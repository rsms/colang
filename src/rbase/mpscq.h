/*
Multiple Producer, Single Consumer Wait-Free Queue by
2015 Daniel Bittman <danielbittman1@gmail.com>: http://dbittman.github.io/

Use this code however you may see fit, as long as you maintain the
comments at the top the source code.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/
#pragma once
#include "rbase.h"
ASSUME_NONNULL_BEGIN

typedef struct MPSCQueue {
  _Atomic size_t count;
  _Atomic size_t head;
  size_t         tail;
  size_t         max;
  void* _Atomic* buffer;
} MPSCQueue;

// MPSCQueueInit initializes a MPSCQueue. cap must be greater than 1.
void MPSCQueueInit(MPSCQueue* q, size_t cap);

// MPSCQueueFree frees internal data (does not free q itselt)
void MPSCQueueFree(MPSCQueue* q);

// MPSCQueueEnqueue adds an item into the queue.
// Returns false on failure; if the queue is full.
// This is safe to call from multiple threads.
bool MPSCQueueEnqueue(MPSCQueue* q, void* item);

// MPSCQueueDequeue removes an item from the queue and returns it.
// THIS IS NOT SAFE TO CALL FROM MULTIPLE THREADS.
// Returns NULL on failure (empty queue).
void* nullable MPSCQueueDequeue(MPSCQueue* q);

// MPSCQueueIsEmpty return true if the queue is empty.
bool MPSCQueueIsEmpty(MPSCQueue* q);

// MPSCQueueLen returns the number of items currently in the queue.
size_t MPSCQueueLen(MPSCQueue* q);

// MPSCQueueCap returns the capacity of the queue.
static inline size_t MPSCQueueCap(MPSCQueue* q) { return q->max; }

ASSUME_NONNULL_END
