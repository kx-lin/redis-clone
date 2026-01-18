#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "utils.h"

constexpr size_t k_max_args = 200 * 1000;

struct Conn {
  int fd = -1;
  // application's intention used by the event loop
  bool want_read = false;
  bool want_write = false;
  bool want_close = false;
  // buffered input and output
  Buffer incoming;
  Buffer outgoing;
};

enum {
  RES_OK = 0,
  RES_ERR = 1,  // error
  RES_NX = 2,   // not found
};

static Conn* handle_accept(int fd) {
  // accept
  struct sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr*)&client_addr, &addrlen);
  if (connfd < 0) {
    return NULL;
  }
  char ip_str[INET_ADDRSTRLEN];  // Buffer to hold the string
                                 // (usually 16 bytes)
  inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
  fprintf(stderr, "new client from %s:%u\n", ip_str,
          ntohs(client_addr.sin_port));
  // set the new connection fd to nonblocking mode
  fd_set_nonblock(connfd);
  // create Conn struct to track state
  Conn* conn = new Conn();
  conn->fd = connfd;
  conn->want_read = true;  // read the first request
  return conn;
}

static bool read_u32(const uint8_t*& cur, const uint8_t* end, uint32_t& out) {
  if (cur + k_header_size > end) {
    return false;
  }
  memcpy(&out, cur, k_header_size);
  cur += 4;
  return true;
}

static bool read_str(const uint8_t*& cur, const uint8_t* end, size_t len,
                     std::string& out) {
  if (cur + len > end) {
    return false;
  }

  out.assign(cur, cur + len);
  cur += len;
  return true;
}

// +------+-----+------+-----+------+-----+-----+------+
// | nstr | len | str1 | len | str2 | ... | len | strn |
// +------+-----+------+-----+------+-----+-----+------+

static int32_t parse_req(const uint8_t* data, size_t size,
                         std::vector<std::string>& out) {
  const uint8_t* end = data + size;
  uint32_t nstr = 0;
  if (!read_u32(data, end, nstr)) {
    return -1;
  }
  if (nstr > k_max_args) {
    return -1;
  }

  while (out.size() < nstr) {
    uint32_t len = 0;
    if (!read_u32(data, end, len)) {
      return -1;
    }
    out.push_back(std::string());
    if (!read_str(data, end, len, out.back())) {
      return -1;
    }
  }

  if (data != end) {
    return -1;  // trailing garbage
  }

  return 0;
}

static std::map<std::string, std::string> g_data;

// +--------+---------+
// | status | data... |
// +--------+---------+
static void do_request(std::vector<std::string>& cmd, Buffer& out) {
  // remember where the msg starts to leave room for the length header
  size_t header_idx = out.size();

  // append placeholder
  uint32_t placeholder = 0;
  out.append((const uint8_t*)&placeholder, k_header_size);

  uint32_t status = RES_OK;

  if (cmd.size() == 2 && cmd[0] == "get") {
    auto it = g_data.find(cmd[1]);
    if (it == g_data.end()) {
      status = RES_NX;  // Not eXists
      out.append((const uint8_t*)&status, 4);
    } else {
      const std::string& val = it->second;
      out.append((const uint8_t*)&status, 4);
      out.append((const uint8_t*)val.data(), val.size());
    }
  } else if (cmd.size() == 3 && cmd[0] == "set") {
    g_data[cmd[1]].swap(cmd[2]);
    out.append((const uint8_t*)&status, 4);
  } else if (cmd.size() == 2 && cmd[0] == "del") {
    g_data.erase(cmd[1]);
    out.append((const uint8_t*)&status, 4);
  } else {
    status = RES_ERR;  // unrecognized command
    out.append((const uint8_t*)&status, 4);
  }

  // size is current - initial - k_header_size
  uint32_t payload_size = (uint32_t)(out.size() - header_idx - k_header_size);

  // patch into placeholder
  memcpy(out.data() + header_idx, &payload_size, k_header_size);
}

static bool try_one_request(Conn* conn) {
  // 3. try to parse the buffer
  // protocol message header
  if (conn->incoming.size() < k_header_size) {
    return false;  // continue to want read
  }
  uint32_t len = 0;
  memcpy(&len, conn->incoming.data(), k_header_size);
  if (len > k_max_msg) {
    conn->want_close = true;  // protocol error
    return false;
  }
  // protocol message body
  if (k_header_size + len > conn->incoming.size()) {
    return false;  // continue to want read
  }

  const uint8_t* request = conn->incoming.data() + k_header_size;

  std::vector<std::string> cmd;
  if (parse_req(request, len, cmd) < 0) {
    conn->want_close = true;
    return false;
  }

  do_request(cmd, conn->outgoing);

  conn->incoming.consume(k_header_size + len);
  return true;
}

static void handle_write(Conn* conn) {
  ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
  if (rv < 0 && errno == EAGAIN) {
    return;  // not ready
  }
  if (rv < 0) {
    conn->want_close = true;
    return;  // error
  }
  // remove written data from outgoing
  conn->outgoing.consume((size_t)rv);
  // update readiness intention
  if (conn->outgoing.size() == 0) {
    conn->want_write = false;
    conn->want_read = true;
  }
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
  conn->incoming.append(buf, (size_t)bytes_read);
  // 3. try to parse the buffer
  // 4. process the parsed message
  // 5. remove the message from conn incoming buffer
  while (try_one_request(conn)) {
  }

  // update readiness intention
  if (conn->outgoing.size() > 0) {
    conn->want_read = false;
    conn->want_write = true;
    // optimistic write: call handle_write immediately to
    // save one poll() syscall. using 'return' enables
    // tail-call optimization, allowing the CPU to jump
    // directly back to the event loop, skipping the return
    // 'hop' through this function
    return handle_write(conn);
  }
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);  // get socket fd
  if (fd < 0) die("socket()");
  int val = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val,
                 sizeof(val)) < 0) {  // socket option to allow same ip:port
                                      // after restart
    die("setsockopt()");
  }
  fd_set_nonblock(fd);

  struct sockaddr_in addr = {};  // address to bind to the socket that the
                                 // server will listen on
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(0);
  if (bind(fd, (const struct sockaddr*)&addr,
           sizeof(addr)) < 0) {  // // binds the socket to this address
    die("bind()");
  }

  if (listen(fd, SOMAXCONN) < 0) {  // server turns on and starts listening for
                                    // incoming connections
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
