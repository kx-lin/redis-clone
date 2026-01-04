#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0); // get socket fd
  if (fd < 0) die("socket()");

  struct sockaddr_in addr = {}; // address of server to connect to
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) { // connect to the ip:port address
    die("connect()");
  }

  char wbuf[] = "hello";
  write(fd, wbuf, strlen(wbuf));

  char rbuf[64] = {};
  ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
  if (n < 0) {
    die("read()");
  }

  printf("server says: %s\n", rbuf);
  close(fd);
}
