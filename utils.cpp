#include "utils.h"
#include <cstdint>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

void fd_set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);

  if (errno) {
    die("fcntl() error");
  }
}

Buffer::Buffer(size_t capacity) {
  uint8_t* ptr = (uint8_t*)malloc(capacity);
  if (!ptr) die("malloc()");
  buffer_begin = data_begin = data_end = ptr;
  buffer_end = ptr + capacity;
}

Buffer::~Buffer() {
  free(buffer_begin);
}

void Buffer::clear() {
  data_begin = data_end = buffer_begin;
}

void Buffer::append(const uint8_t* data, size_t len) {
  if (free_space() < len) {
    size_t data_size = size();

    if (capacity() >= data_size + len) {
      memmove(buffer_begin, data_begin, data_size);
      data_begin = buffer_begin;
      data_end = buffer_begin + data_size;
    } else {
      size_t new_capacity = capacity() * 2 + len;
      uint8_t* new_ptr = (uint8_t*)realloc(buffer_begin, new_capacity);
      if (!new_ptr) die("realloc()");

      data_begin = new_ptr + (data_begin - buffer_begin);
      data_end = new_ptr + (data_end - buffer_begin);
      buffer_begin = new_ptr;
      buffer_end = new_ptr + new_capacity;
    }
  }

  memcpy(data_end, data, len);
  data_end += len;
}

void Buffer::consume(size_t len) {
  data_begin += len;
  if (data_begin == data_end) {
    data_begin = data_end = buffer_begin;
  }
}

int32_t read_all(int fd, uint8_t *buf, size_t n) {
  while (n > 0) {
    ssize_t bytes_read = read(fd, buf, n);
    if (bytes_read <= 0) {
      if (errno == EINTR)
        continue; // interrupted by signal
      return -1;  // EOF (== 0) before n bytes sent is an error
    }
    n -= bytes_read;
    buf += bytes_read;
  }

  return 0;
}

int32_t write_all(int fd, uint8_t *buf, size_t n) {
  while (n > 0) {
    ssize_t bytes_written = write(fd, buf, n);
    if (bytes_written <= 0) {
      if (errno == EINTR)
        continue;
      return -1;
    };
    n -= bytes_written;
    buf += bytes_written;
  }

  return 0;
}
