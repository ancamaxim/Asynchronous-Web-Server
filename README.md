Implemented a web server that uses the following advanced I/O operations:

* Asynchronous operations on files
* Non-blocking operations on sockets
* Zero-copying
* Multiplexing I/O operations

The server implements a limited functionality of the HTTP protocol: passing files to clients.

The web server will use the multiplexing API to wait for connections from clients - epoll.

On the established connections, requests from clients will be received and then responses will be distributed to them.

[Statement](https://cs-pub-ro.github.io/operating-systems/Assignments/Asynchronous%20Web%20Server/)
