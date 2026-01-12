#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <string>

#include "utils.h"

static int32_t send_req(int fd, const uint8_t* text, size_t len) {
  if (len > k_max_msg) {
    msg("too long");
    return -1;
  }

  // send request
  Buffer wbuf;
  wbuf.append((const uint8_t*)&len, k_header_size);
  wbuf.append(text, len);
  return write_all(fd, wbuf.data(), wbuf.size());
}

static int32_t read_res(int fd) {
  // protocol message header
  std::vector<uint8_t> rbuf;
  rbuf.resize(k_header_size);
  errno = 0;
  int32_t err = read_all(fd, rbuf.data(), k_header_size);
  if (err) {
    msg(errno == 0 ? "EOF" : "read() error");
    return err;
  }

  uint32_t len = 0;
  memcpy(&len, rbuf.data(),
         k_header_size);  // assume client and server same endianness
  if (len > k_max_msg) {
    msg("too long");
    return -1;
  }

  // protocol message body
  rbuf.resize(k_header_size + len);
  err = read_all(fd, rbuf.data() + k_header_size, len);
  if (err) {
    msg("read() error");
    return err;
  }

  // process request
  printf("len:%u data:%.*s\n", len, len < 100 ? len : 100,
         rbuf.data() + k_header_size);
  return 0;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);  // get socket fd
  if (fd < 0) die("socket()");

  struct sockaddr_in addr = {};  // address of server to connect to
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (const struct sockaddr*)&addr, sizeof(addr)) <
      0) {  // connect to the ip:port address
    die("connect()");
  }

  // multiple pipelined requests
  std::vector<std::string> query_list = {
      "hello1",
      "hello2",
      "hello3",
      // a large message requires multiple event loop iterations
      std::string(k_max_msg, 'z'),
      "hello5",
  };
  for (const std::string& s : query_list) {
    int32_t err = send_req(fd, (uint8_t*)s.data(), s.size());
    if (err) {
      goto L_DONE;
    }
  }
  for (size_t i = 0; i < query_list.size(); ++i) {
    int32_t err = read_res(fd);
    if (err) {
      goto L_DONE;
    }
  }

L_DONE:
  close(fd);
  return 0;
}
