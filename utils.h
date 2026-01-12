#ifndef UTILS_H
#define UTILS_H

#include <cstdint>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

constexpr size_t k_max_msg = 32 << 20;
constexpr size_t k_header_size = 4;

struct Buffer {
  uint8_t *buffer_begin = nullptr;
  uint8_t *buffer_end = nullptr;
  uint8_t *data_begin = nullptr;
  uint8_t *data_end = nullptr;

  Buffer(size_t capacity = 16 * 1024);
  
  ~Buffer();

  void clear();

  size_t size() const { return data_end - data_begin; }
  size_t capacity() const { return buffer_end - buffer_begin; }
  size_t free_space() const { return buffer_end - data_end; }
  uint8_t* data() { return data_begin; }

  void append(const uint8_t* data, size_t len);
  void consume(size_t len);
};

void die(const char *msg);
void msg(const char *msg);
void fd_set_nonblock(int fd);
int32_t read_all(int fd, uint8_t *buf, size_t n);
int32_t write_all(int fd, uint8_t *buf, size_t n);

#endif
