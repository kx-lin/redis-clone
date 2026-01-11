#ifndef UTILS_H
#define UTILS_H

#include <cstdint>
#include <stdint.h>
#include <stddef.h>
#include <vector>

constexpr size_t k_max_msg = 4096;
constexpr size_t k_header_size = 4;

void die(const char* msg);
void msg(const char* msg);
void fd_set_nonblock(int fd);
void buf_append(std::vector<uint8_t>& buf, const uint8_t* data, size_t len);
void buf_consume(std::vector<uint8_t>& buf, size_t len);
int32_t read_all(int fd, char* buf, size_t n);
int32_t write_all(int fd, const char* buf, size_t n);

#endif
