#include "utils.h"
#include <cstdint>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

void die(const char* msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

void msg(const char* msg) {
  fprintf(stderr, "%s\n", msg);
}

void fd_set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags);

  if (errno) {
    die("fcntl() error");
  }
}

void buf_append(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
  buf.insert(buf.end(), data, data + len);
}

void buf_consume(std::vector<uint8_t>& buf, size_t len) {
  buf.erase(buf.begin(), buf.begin() + len);
}

int32_t read_all(int fd, char* buf, size_t n) {
  while (n > 0) {
    ssize_t bytes_read = read(fd, buf, n);
    if (bytes_read <= 0) {
      if (errno == EINTR) continue; // interrupted by signal
      return -1; // EOF (== 0) before n bytes sent is an error
    }
    n -= bytes_read;
    buf += bytes_read;
  }

  return 0;
}

int32_t write_all(int fd, const char* buf, size_t n) {
  while (n > 0) {
    ssize_t bytes_written = write(fd, buf, n);
    if (bytes_written <= 0) {
      if (errno == EINTR) continue;
      return -1;
    };
    n -= bytes_written;
    buf += bytes_written;
  }

  return 0;
}
