# redis-clone

## Milestone 1: Socket API Setup
This stage covers the basic handshake between the server and the client.

### Server Setup
1. **`socket()`**: Create the file descriptor.
2. **`setsockopt()`**: Enable `SO_REUSEADDR` to allow immediate restarts.
3. **`bind()`**: Associate the socket with an IP and Port.
4. **`listen()`**: Wait for incoming connection requests.
5. **`accept()`**: Block until a client connects and return a new client-specific descriptor.

### Client Setup
1. **`socket()`**: Create the file descriptor.
2. **`connect()`**: Connect to the server's IP and Port.

### Communication
* **`read()` / `write()`**: Exchange data between descriptors.
* **`close()`**: Release the file descriptors.

---

## Milestone 2: Protocol Framing & The Byte Stream

This milestone addresses the transition from raw byte streams to a reliable **Application Protocol**.

### 1. TCP is a Byte Stream, Not a Message Stream
* **The Problem:** TCP does not preserve message boundaries. A single `write()` from a client might be split across multiple `read()` calls, or multiple `write()` calls might be merged into one.
* **The Solution:** We implement **Protocol Framing** using a length-prefix.

### 2. Reliable I/O with `read_full` and `write_all`
Standard syscalls can return "short" (fewer than requested bytes) even without an error.
* **`read_full`**: Loops until the exact number of bytes requested is filled.
* **`write_all`**: Loops until the entire buffer is sent to the kernel.

### 3. Length-Prefixed Protocol
To parse messages efficiently, we use a binary header:
* **Header (4 Bytes):** A little-endian integer representing the length of the message.
* **Payload (Variable):** The actual command or data.

---

## Milestone 3: Non-Blocking I/O & The Event Loop

This milestone represents a major architectural shift from a "one-at-a-time" blocking server to a high-performance **Event-Driven** server. We transitioned from waiting on data to reacting to readiness notifications.

### 1. The Shift to Non-Blocking I/O
* **The Problem:** In a blocking server, `read()` or `accept()` halts the entire program until data arrives. This makes the server vulnerable to "slow clients" who can hang the entire system.
* **The Solution:** We set both the listening and client sockets to **Non-Blocking mode** (`O_NONBLOCK`). Now, these syscalls return immediately with an error (`EAGAIN`) if no data is ready, allowing the thread to move on to other tasks.


### 2. Multiplexing with `poll()`
* **The Event Loop:** Instead of checking sockets one by one (which wastes CPU), we use `poll()` to monitor all file descriptors at once.
* **Readiness-Based Execution:** The program "sleeps" inside `poll()` and wakes up only when the OS confirms that a specific socket has data to read or space to write. This allows a single thread to handle multiple concurrent connections.

### 3. Managing State with `struct Conn`
* **Explicit Context:** In a non-blocking world, a single request can be "fragmented" (e.g., the header arrives in one loop, the body in the next). We created a `Conn` struct to persist the state of each client.
* **Buffered I/O:** We introduced `incoming` and `outgoing` buffers. The server reads what it can into the `incoming` buffer and only processes the request once the buffer contains a full message.


### 4. Intent-Driven Logic
* **State Transitions:** We implemented a state-tracking system using `want_read` and `want_write` flags. 
    * The server starts by wanting to **Read** a request.
    * Once a request is processed, it switches to wanting to **Write** the response.
    * This prevents the server from trying to write to a socket before the response is ready.

### 5. Deferred Connection Cleanup
* **Safe Destruction:** We learned that you cannot delete a connection while you are still iterating through the active list of file descriptors. 
* **The `want_close` Flag:** By marking connections for closure and performing the actual `close()` and `delete` at the end of the loop, we avoid memory corruption and iterator invalidation.

### Summary of the "New" Loop Flow
1.  **Poll Setup:** Populate `pollfd` structures based on whether each connection is currently in a "Read" or "Write" state.
2.  **Wait:** Call `poll()` to wait for any activity.
3.  **Handle:** 
    * **New Clients:** `accept()` them and add their state to the `fd2conn` map.
    * **Existing Clients:** Perform `read()` or `write()` operations and update their state based on the protocol.
4.  **Cleanup:** Sweep through the connection list to remove any clients marked with `want_close` or those that encountered errors.
