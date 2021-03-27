#include "rbase.h"


int ReaderOpen(Reader* r, const char* filename) {
  int fd = STDIN_FILENO;
  if (strcmp(filename, "-") != 0) {
    fd = open(filename, O_RDONLY);
    if (fd < 0)
      return errno;
  }
  r->fd = fd;
  return 0;
}

int ReaderClose(Reader* r) {
  auto fd = r->fd;
  r->fd = -1;
  if (fd < 0 || fd == STDIN_FILENO)
    return 0;
  return close(fd);
}

int ReaderRead(Reader* r) {
  int nread = read(r->fd, r->buf, countof(r->buf));
  if (nread < 0) {
    r->err = errno;
  }
  return nread;
}


void WriterInit(Writer* w, int fd) {
  w->fd = fd;
  w->len = 0;
}

int WriterFlush(Writer* w) {
  if (w->len == 0)
    return 0;
  int nwrite = write(w->fd, w->buf, w->len);
  if (nwrite > 0) {
    // shift what remains in buffer after partial write (unlikely)
    if (nwrite < w->len)
      memmove(w->buf, &w->buf[nwrite], w->len - nwrite);
    w->len -= nwrite;
  }
  return nwrite;
}

int WriterClose(Writer* w) {
  int status = 0;
  if (w->len)
    status = WriterFlush(w);
  status = MIN(status, close(w->fd));
  w->fd = -1;
  return status;
}

int WriterWrite(Writer* w, const void* data, int inlen) {
  // when cap is less than or equal to this, flush
  const int flush_low = countof(w->buf) / 8; // 1/8th
  int cap = countof(w->buf) - w->len;
  while (inlen > 0) {
    // copy data to buffer
    int chunklen = MIN(inlen, cap);
    memcpy(&w->buf[w->len], data, chunklen);
    w->len += chunklen;
    cap -= chunklen;
    inlen -= chunklen;
    // flush if needed
    if (cap <= flush_low) {
      auto nwrite = WriterFlush(w);
      if (nwrite < 0)
        return -1;
      cap += nwrite;
    }
    // advance data pointer
    data = &((const u8*)data)[chunklen];
  }
  return 0;
}
