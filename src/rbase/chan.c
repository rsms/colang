#include "rbase.h"

/*
    // Buffered channel properties
    queue_t*         queue;

    // Unbuffered channel properties
    pthread_mutex_t  r_mu;
    pthread_mutex_t  w_mu;
    void*            data;
*/

// Chan.flags
#define CHAN_CLOSED   1
#define CHAN_BUFFERED 2

#define CHAN_GET_FLAGS(ch) atomic_load_explicit(&(ch)->flags, memory_order_acquire)

#define CHAN_IS_CLOSED(ch)   (CHAN_GET_FLAGS(ch) & CHAN_CLOSED)
#define CHAN_IS_BUFFERED(ch) (CHAN_GET_FLAGS(ch) & CHAN_BUFFERED)

typedef struct Wait {
  mtx_t      mu;
  cnd_t      cond;
  atomic_i32 count;
} Wait;

static inline int WaitInit(Wait* w) {
  int s = mtx_init(&w->mu, mtx_plain);
  return s + cnd_init(&w->cond);
}

static inline void WaitFree(Wait* w) {
  cnd_destroy(&w->cond);
  mtx_destroy(&w->mu);
}

static void WaitWait(Wait* w) {
  mtx_lock(&w->mu);
  atomic_fetch_add_explicit(&w->count, 1, memory_order_acquire);
  cnd_wait(&w->cond, &w->mu);
  mtx_unlock(&w->mu);
}

static bool WaitWake(Wait* w) {
  if (atomic_load_explicit(&w->count, memory_order_relaxed) != 0) {
    i32 c = atomic_fetch_sub_explicit(&w->count, 1, memory_order_acquire);
    if (c >= 0)
      return cnd_signal(&w->cond) == 0;
    // race lost
    atomic_fetch_add_explicit(&w->count, 1, memory_order_acquire);
  }
  return false;
}

typedef struct Chan {
  Mem        mem;       // memory allocator this belongs to (immutable)
  u32        dataqsiz;  // size of the circular queue (immutable)
  u16        elemsize;  // size of messages (immutable)
  atomic_u32 qcount;    // total data in the queue
  u8*        buf;       // points to an array of dataqsiz elements of size elemsize
  atomic_u32 closed;    // 1 when closed
  atomic_u32 sendx;     // send index
  u32        recvx;     // receive index

  Wait  recvw;     // recv waiters
  Wait  sendw;     // send waiters

  mtx_t lock;      // protects all fields in Chan
} Chan;

Chan* ChanNew(Mem mem, size_t elemsize, u32 cap) {
  if (elemsize > 0xFFFF)
    panic("elemsize too large");

  size_t bufz = ((size_t)cap) * elemsize;
  Chan* c = (Chan*)memalloc(mem, sizeof(Chan) + bufz);
  c->mem = mem;
  if (bufz != 0) // buffered
    c->buf = ((u8*)c) + sizeof(Chan);
  c->dataqsiz = cap;
  c->elemsize = (u16)elemsize;

  if (WaitInit(&c->recvw) + WaitInit(&c->sendw) + mtx_init(&c->lock, mtx_plain) != 0)
    panic("ChanInit");

  return c;
}

void ChanFree(Chan* c) {
  WaitFree(&c->recvw);
  WaitFree(&c->sendw);
  memfree(c->mem, c);
}

// is_full reports whether a send on c would block (that is, the channel is full).
// It uses a single word-sized read of mutable state, so although
// the answer is instantaneously true, the correct answer may have changed
// by the time the calling function receives the return value.
static bool is_full(const Chan* c) {
  // c.dataqsiz is immutable (never written after the channel is created)
  // so it is safe to read at any time during channel operation.
  if (c->dataqsiz == 0)
    return atomic_load_explicit(&c->recvw.count, memory_order_relaxed) == 0;
  return atomic_load_explicit(&c->qcount, memory_order_relaxed) == c->dataqsiz;
}

// empty reports whether a read from c would block (that is, the channel is empty)
static bool is_empty(Chan* c) {
  // dataqsiz is immutable
  if (c->dataqsiz == 0)
    return atomic_load_explicit(&c->sendw.count, memory_order_relaxed) == 0;
  return atomic_load_explicit(&c->qcount, memory_order_relaxed) == 0;
}

// chanbuf(c, i) is pointer to the i'th slot in the buffer.
static void* chanbuf(Chan* c, u32 i) {
  return (void*)(c->buf + (i * c->elemsize));
}

// go notes
//   typedmemmove copies a value of type t to dst from src.
//   typedmemmove(typ *_type, dst, src unsafe.Pointer)

static bool chansend(Chan* c, const void* elem, bool block) {
  bool ok = false;
  while (1) {

    // Fast path: check for failed non-blocking operation without acquiring the lock.
    //
    // After observing that the channel is not closed, we observe that the channel is
    // not ready for sending. Each of these observations is a single word-sized read
    // (first c.closed and second full()).
    // Because a closed channel cannot transition from 'ready for sending' to
    // 'not ready for sending', even if the channel is closed between the two observations,
    // they imply a moment between the two when the channel was both not yet closed
    // and not ready for sending. We behave as if we observed the channel at that moment,
    // and report that the send cannot proceed.
    //
    // It is okay if the reads are reordered here: if we observe that the channel is not
    // ready for sending and then observe that it is not closed, that implies that the
    // channel wasn't closed during the first observation. However, nothing here
    // guarantees forward progress. We rely on the side effects of lock release in
    // chanrecv() and closechan() to update this thread's view of c.closed and full().
    if (!block && c->closed == 0 && is_full(c)) {
      dlog("[chansend#%p] full (nonblock)", &ok);
      return false;
    }

    dlog("[chansend#%p] acquiring c->lock", &ok);
    mtx_lock(&c->lock);

    if (c->closed != 0) {
      mtx_unlock(&c->lock);
      panic("send on closed channel");
      // UNREACHABLE;
    }

    // if (c->qcount < c->dataqsiz)
    if (atomic_load_explicit(&c->qcount, memory_order_relaxed) < c->dataqsiz) {
      // Space is available in the channel buffer. Enqueue the element to send.
      dlog("[chansend#%p] space in buffer; enqueued", &ok);
      auto qp = chanbuf(c, c->sendx);
      memcpy(qp, elem, c->elemsize);

      // c->sendx++;
      u32 sendx = atomic_fetch_add_explicit(&c->sendx, 1, memory_order_acquire);
      if (sendx == c->dataqsiz) {
        // wrap around
        // c->sendx = 0;
        atomic_store_explicit(&c->sendx, 0, memory_order_release);
      }

      // c->qcount++;
      atomic_fetch_add_explicit(&c->qcount, 1, memory_order_relaxed);

      cnd_signal(&c->recvw.cond); // wake someone waiting for recv
      ok = true;
      break;
    }

    dlog("[chansend#%p] no space; buffer is full", &ok);

    if (!block)
      break;

    // block
    mtx_unlock(&c->lock);
    dlog("[chansend#%p] block; waiting on sendw", &ok);
    WaitWait(&c->sendw);
    dlog("[chansend#%p] unblocked; retrying", &ok);
    // loop
  } // while(1)

  mtx_unlock(&c->lock);
  return ok;

  // if (cnd_signal(&c->recvw.cond) == 0) {
  //   // woke a waiting receiver
  //   // atomic_load_explicit(&c->recvw.count, memory_order_relaxed);
  // }


  // if (flags & CHAN_IS_CLOSED(ch)) {
  //   // Note: although we don't raise SIGPIPE as libc functions to,
  //   // EPIPE is still better than EBADF for signalling "channel is closed."
  //   errno = EPIPE;
  //   return -1;
  // }
  // // Note: intentionally loading flags without atomic instructions as
  // // CHAN_BUFFERED doesn't change after ChanInit.
  // if (ch->flags & CHAN_CLOSED) {
  //   while (!MPSCQueueEnqueue(&ch->q, msg)) {
  //     // queue is full; block until something is dequeued
  //     dlog("[ChanSend] full; waiting on sendcond");
  //     mtx_lock(&ch->sendmu);
  //     cnd_wait(&ch->sendcond, &ch->sendmu);
  //     mtx_unlock(&ch->sendmu);
  //   }
  //   dlog("[ChanSend] done; msg=%p", msg);
  //   cnd_signal(&ch->recvcond);
  // } else {
  //   // TODO
  // }
}


static void* nullable chanrecv(Chan* c, bool block, bool* closed) {
  // Fast path: check for failed non-blocking operation without acquiring the lock.
  if (!block && is_empty(c)) {
    // After observing that the channel is not ready for receiving, we observe whether the
    // channel is closed.
    //
    // Reordering of these checks could lead to incorrect behavior when racing with a close.
    // For example, if the channel was open and not empty, was closed, and then drained,
    // reordered reads could incorrectly indicate "open and empty". To prevent reordering,
    // we use atomic loads for both checks, and rely on emptying and closing to happen in
    // separate critical sections under the same lock.  This assumption fails when closing
    // an unbuffered channel with a blocked send, but that is an error condition anyway.
    //if atomic.Load(&c->closed) == 0
    if (atomic_load_explicit(&c->closed, memory_order_acquire) == 0) {
      // Because a channel cannot be reopened, the later observation of the channel
      // being not closed implies that it was also not closed at the moment of the
      // first observation. We behave as if we observed the channel at that moment
      // and report that the receive cannot proceed.
      dlog("[chanrecv] closed");
      *closed = true;
      return NULL;
    }
    // The channel is irreversibly closed. Re-check whether the channel has any pending data
    // to receive, which could have arrived between the empty and closed checks above.
    // Sequential consistency is also required here, when racing with such a send.
    if (is_empty(c)) {
      dlog("[chanrecv] empty (nonblock)");
      return NULL;
    }
  }

  mtx_lock(&c->lock);

  if (c->closed != 0 && c->qcount == 0) {
    mtx_unlock(&c->lock);
    dlog("[chanrecv] closed");
    *closed = true;
    return NULL;
  }

  if (WaitWake(&c->sendw)) {
    dlog("[chanrecv] woke up sendw");
  }

  dlog("[chanrecv] TODO rest");
  mtx_unlock(&c->lock);
  return NULL;
}

bool ChanSend(Chan* c, const void* elem) {
  return chansend(c, elem, /*block*/true);
}

void* ChanRecv(Chan* ch) {
  bool closed = false;
  return chanrecv(ch, /*block*/true, &closed);
  // while (1) {
  //   auto msg = MPSCQueueDequeue(&ch->q);
  //   cnd_signal(&ch->sendcond);
  //   if (msg) {
  //     dlog("[ChanRecv] done; msg=%p", msg);
  //     return msg;
  //   }
  //   dlog("[ChanRecv] empty; waiting on recvcond");
  //   mtx_lock(&ch->recvmu);
  //   cnd_wait(&ch->recvcond, &ch->recvmu);
  //   mtx_unlock(&ch->recvmu);
  // }
  // return NULL;
}
