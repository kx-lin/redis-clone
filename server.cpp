#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "utils.h"
#include <vector>
#include <poll.h>

struct Conn {
  int fd = -1;
  // application's intention used by the event loop
  bool want_read = false;
  bool want_write = false;
  bool want_close = false; 
  // buffered input and output
  std::vector<uint8_t> incoming;
  std::vector<uint8_t> outgoing;
};

static Conn* handle_accept(int fd) {
  // accept
  struct sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr*)&client_addr, &addrlen);
  if (connfd < 0) {
    return NULL;
  }
  // set the new connection fd to nonblocking mode
  fd_set_nonblock(connfd);
  // create Conn struct to track state
  Conn* conn = new Conn();
  conn->fd = connfd;
  conn->want_read = true; // read the first request
  return conn;
}

static bool try_one_request(Conn* conn) {
  // 3. try to parse the buffer
  // protocol message header
  if (conn->incoming.size() < k_header_size) {
    return false; // continue to want read
  }
  uint32_t len = 0;
  memcpy(&len, conn->incoming.data(), k_header_size);
  if (len > k_max_msg) {
    conn->want_close = true; // protocol error
    return false;
  }
  // protocol message body
  if (k_header_size + len > conn->incoming.size()) {
    return false; // continue to want read
  }

  const uint8_t* request = &conn->incoming[4];
  printf("client says: %s\n", request);
  // 4. process the parsed message. simple echo
  buf_append(conn->outgoing, (const uint8_t*)&len, 4);
  buf_append(conn->outgoing, request, len);
  // 5. remove the message from conn incoming buffer
  buf_consume(conn->incoming, k_header_size + len);
  return true;
}

static void handle_read(Conn* conn) {
  // 1. do a nonblocking read
  uint8_t buf[64 * 1024];
  ssize_t bytes_read = read(conn->fd, buf, sizeof(buf));
  if (bytes_read <= 0) {
    conn->want_close = true;
    return;
  }
  // 2. add new data to conn incoming buffer
  buf_append(conn->incoming, buf, (size_t)bytes_read);
  // 3. try to parse the buffer
  // 4. process the parsed message
  // 5. remove the message from conn incoming buffer
  try_one_request(conn);

  // update readiness intention
  if (conn->outgoing.size() > 0) {
    conn->want_read = false;
    conn->want_write = true;
  }
}

static void handle_write(Conn* conn) {
  ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
  if (rv < 0) {
    conn->want_close = true;
    return;
  }
  // remove written data from outgoing
  buf_consume(conn->outgoing, (size_t)rv);
  // update readiness intention
  if (conn->outgoing.size() == 0) {
    conn->want_write = false;
    conn->want_read = true;
  }
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0); // get socket fd
  if (fd < 0) die("socket()");
  int val = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) { // socket option to allow same ip:port after restart
    die("setsockopt()");
  }
  fd_set_nonblock(fd);

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

  // mapping of fd to Conn
  std::vector<Conn*> fd2conn;
  // event loop
  std::vector<struct pollfd> poll_args;
  while (true) {
    // prepare poll args
    poll_args.clear();
    // put the listening socket in first position
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    // the rest are connection sockets
    for (Conn* conn : fd2conn) {
      if (!conn) continue;
      struct pollfd pfd = {conn->fd, POLLERR, 0};
      // poll() flags depending on application intent
      if (conn->want_read) {
        pfd.events |= POLLIN;
      }
      if (conn->want_write) {
        pfd.events |= POLLOUT;
      }
      poll_args.push_back(pfd);
    }

    // wait for readiness
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
    if (rv < 0 && errno == EINTR) continue;
    if (rv < 0) die("poll()");

    // handle listening socket
    if (poll_args[0].revents & POLLIN) {
      if (Conn* conn = handle_accept(fd)) {
        // add into mapping of fd to Conn
        if (fd2conn.size() <= (size_t)conn->fd) {
          fd2conn.resize(2 * conn->fd);
        }
        fd2conn[conn->fd] = conn;
      }
    }

    // handle connection sockets
    for (size_t i = 1; i < poll_args.size(); i++) {
      uint32_t ready = poll_args[i].revents;
      Conn* conn = fd2conn[poll_args[i].fd];
      if (ready & POLLIN) {
        handle_read(conn);
      }
      if (ready & POLLOUT) {
        handle_write(conn);
      }
      if (ready & POLLERR || conn->want_close) {
        close(conn->fd);
        fd2conn[conn->fd] = NULL;
        delete conn;
      }
    }
  }

  return 0;
}
