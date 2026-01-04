# redis-clone

# Milestone 1: Socket API Setup

## Server Setup
1. **`socket()`**: Create the file descriptor.
2. **`setsockopt()`**: Enable `SO_REUSEADDR` to allow immediate restarts.
3. **`bind()`**: Associate the socket with an IP and Port.
4. **`listen()`**: Wait for incoming connection requests.
5. **`accept()`**: Block until a client connects and return a new client-specific descriptor.

## Client Setup
1. **`socket()`**: Create the file descriptor.
2. **`connect()`**: Connect to the server's IP and Port.

## Communication
* **`read()` / `write()`**: Exchange data between descriptors.
* **`close()`**: Release the file descriptors.
