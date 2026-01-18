#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <string>

#include "utils.h"

static int32_t send_req(int fd, std::vector<std::string>& cmd) {
  uint32_t len = 4;
  for (const std::string& s : cmd) {
    len += 4 + s.size();
  }

  if (len > k_max_msg) {
    msg("too long");
    return -1;
  }

  // send request
  Buffer wbuf;
  wbuf.append((const uint8_t*)&len, k_header_size);
  uint32_t n = cmd.size();
  wbuf.append((const uint8_t*)&n, k_header_size);
  for (const std::string& s : cmd) {
    uint32_t p = (uint32_t)s.size();
    wbuf.append((const uint8_t*)&p, k_header_size);
    wbuf.append((const uint8_t*)s.data(), s.size());
  }
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

  // print the result
  uint32_t rescode = 0;
  if (len < 4) {
    msg("bad response");
    return -1;
  }
  memcpy(&rescode, &rbuf[4], 4);
  printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
  return 0;
}

int main(int argc, char** argv) {
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

  std::vector<std::vector<std::string>> pipeline = {
      {"set", "k1", "v1"}, {"get", "k1"}, {"set", "k2", "v2"},
      {"get", "k2"},       {"del", "k1"}, {"get", "k1"}};

  printf("Sending %zu pipelined requests...\n", pipeline.size());
  for (auto& cmd : pipeline) {
    if (send_req(fd, cmd) != 0) {
      msg("send_req error");
      goto L_DONE;
    }
  }

  printf("Reading responses back...\n");
  for (size_t i = 0; i < pipeline.size(); i++) {
    printf("Response %zu: ", i + 1);
    if (read_res(fd) != 0) {
      msg("read_res error");
      goto L_DONE;
    }
  }

L_DONE:
  close(fd);
  return 0;
}
