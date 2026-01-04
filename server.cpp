#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

// one read and write
void do_something(int connfd) {
  char rbuf[64] = {};
  ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
  if (n < 0) {
    msg("read() error");
    return;
  }
  printf("client says: %s\n", rbuf);

  char wbuf[] = "world";
  write(connfd, wbuf, strlen(wbuf));
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0); // get socket fd
  if (fd < 0) die("socket()");
  int val = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) { // socket option to allow same ip:port after restart
    die("setsockopt()");
  }

  struct sockaddr_in addr = {}; // address to bind to the socket that the server will listen on
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(0);
  if (bind(fd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) { // // binds the socket to this address
    die("bind()");
  }

  if (listen(fd, SOMAXCONN) < 0) { // server turns on and starts listening for incoming connections
    die("listen()");
  }

  // main loop for accepting connections
  while (true) {
    struct sockaddr_in client_addr = {}; // structure for client address
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr*)&client_addr, &addrlen);
    if (connfd < 0) continue; // error with connection, ignore

    do_something(connfd); // process the client
    close(connfd);
  }
}
