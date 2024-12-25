// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */
	static const char header[] = "HTTP/1.1 200 OK\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n";

	sprintf(conn->send_buffer, header, conn->file_size);
	conn->send_len = strlen(conn->send_buffer);
	conn->state = STATE_SENDING_HEADER;
	dlog(LOG_DEBUG, "Prepared reply header.\n");
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	static const char header[] = "HTTP/1.1 404 Not Found\r\n"
		"Content-Length: 0\r\n"
		"Connection: close\r\n"
		"\r\n";

	strcpy(conn->send_buffer, header);
	conn->send_len = strlen(conn->send_buffer);
	conn->state = STATE_SENDING_404;
	dlog(LOG_DEBUG, "Prepared 404 header.\n");
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (strstr(conn->filename, AWS_REL_STATIC_FOLDER))
		return RESOURCE_TYPE_STATIC;
	else if (strstr(conn->filename, AWS_REL_DYNAMIC_FOLDER))
		return RESOURCE_TYPE_DYNAMIC;
	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */
	struct connection *conn = malloc(sizeof(*conn));

	DIE(!conn, "malloc");

	conn->sockfd = sockfd;
	conn->eventfd = eventfd(0, 0);
	conn->state = STATE_INITIAL;
	// create AIO context capable of handling 1 event
	io_setup(1, &(conn->ctx));
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);
	conn->recv_len = 0;

	return conn;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	close(conn->fd);
	close(conn->sockfd);
	close(conn->eventfd);
	conn->state = STATE_CONNECTION_CLOSED;
	io_destroy(conn->ctx);
	w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	w_epoll_remove_fd(epollfd, conn->eventfd);
	dlog(LOG_DEBUG, "Connection removed\n");
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */
	static int connectfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;
	/* TODO: Accept new connection. */
	connectfd = accept(listenfd, (SSA *) &addr, &addrlen);
	DIE(connectfd < 0, "accept");
	dlog(LOG_ERR, "Accepted connection from: %s:%d\n",
		inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	/* TODO: Set socket to be non-blocking. */
	fcntl(connectfd, F_SETFL, fcntl(connectfd, F_GETFL, 0) | O_NONBLOCK);
	/* TODO: Instantiate new connection handler. */
	conn = connection_create(connectfd);
	/* TODO: Add socket to epoll. */
	rc = w_epoll_add_ptr_in(epollfd, connectfd, conn);
	DIE(rc < 0, "w_epoll_add_in");
	/* TODO: Initialize HTTP_REQUEST parser. */
	http_parser_init(&(conn->request_parser), HTTP_REQUEST);
	conn->request_parser.data = conn;
	dlog(LOG_DEBUG, "Connection created\n");
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */
	int rc;

	conn->state = STATE_RECEIVING_DATA;

	while (1) {
		rc = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ - conn->recv_len, 0);
		if (rc <= 0)
			break;
		conn->recv_len += rc;
	}
	conn->state = STATE_REQUEST_RECEIVED;
	dlog(LOG_DEBUG, "Received data\n");
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */
	int rc;
	struct stat file_stat;

	rc = open(conn->filename, O_RDONLY, 0666);
	if (rc < 0) {
		dlog(LOG_DEBUG, "Invalid file\n");
		return rc;
	}
	conn->fd = rc;
	fstat(conn->fd, &file_stat);
	conn->file_size = file_stat.st_size;
	conn->send_pos = 0;
	conn->file_pos = 0;
	dlog(LOG_DEBUG, "File opened\n");
	return rc;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	int rc;

	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};
	rc = http_parser_execute(&(conn->request_parser), &settings_on_path,
								conn->recv_buffer, conn->recv_len);
	strcpy(conn->filename, conn->request_path + 1);
	dlog(LOG_DEBUG, "Parsed header\n");
	return rc;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */
	int rc;

	dlog(LOG_DEBUG, "Send static\n");
	conn->file_pos = 0;
	while (1) {
		rc = sendfile(conn->sockfd, conn->fd, NULL, BUFSIZ);
		if (rc == 0)
			return STATE_DATA_SENT;
		if (rc > 0)
			conn->file_pos += rc;
	}
	return 0;
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	int rc;

	conn->send_pos = 0;
	while (conn->send_pos < conn->send_len && conn->send_pos < BUFSIZ) {
		rc = send(conn->sockfd, conn->send_buffer + conn->send_pos, conn->send_len - conn->send_pos, 0);
		if (rc == 0)
			break;
		if (rc > 0)
			conn->send_pos += rc;
	}

	return conn->send_pos;
}


int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	dlog(LOG_DEBUG, "Send dinamic\n");
	size_t total = 0;
	struct io_event ev[1];

	conn->piocb[0] = &(conn->iocb);
	// init iocb for each connection
	memset(&(conn->iocb), 0, sizeof(conn->iocb));

	while (total < conn->file_size) {
		io_prep_pread(&(conn->iocb), conn->fd, conn->send_buffer, BUFSIZ, total);
		// starts AIO operations in piocb (reading from file)
		io_submit(conn->ctx, 1, conn->piocb);
		io_getevents(conn->ctx, 1, 1, ev, NULL);
		total += ev->res;
		io_prep_pwrite(&(conn->iocb), conn->sockfd, conn->send_buffer, ev->res, 0);
		io_submit(conn->ctx, 1, conn->piocb);
		io_getevents(conn->ctx, 1, 1, ev, NULL);
	}
	conn->state = STATE_DATA_SENT;
	return 0;
}


void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */
	int rc;

	while (conn->state != STATE_CONNECTION_CLOSED && !OUT_STATE(conn->state)) {
		switch (conn->state) {
		case STATE_INITIAL:
			receive_data(conn);
			break;
		case STATE_RECEIVING_DATA:
			receive_data(conn);
			break;
		case STATE_REQUEST_RECEIVED:
			rc = parse_header(conn);
			if (rc < 0)
				connection_remove(conn);
			rc = connection_open_file(conn);
			conn->res_type = connection_get_resource_type(conn);
			if (rc < 0 || conn->res_type == RESOURCE_TYPE_NONE) {
				conn->res_type = RESOURCE_TYPE_NONE;
				connection_prepare_send_404(conn);
			} else {
				connection_prepare_send_reply_header(conn);
			}
			break;
		default:
			return;
		}
	}
	if (OUT_STATE(conn->state))
		w_epoll_update_ptr_out(epollfd, conn->sockfd, conn);
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */
	int rc;

	dlog(LOG_DEBUG, "Handle output\n");
	while (conn->state != STATE_CONNECTION_CLOSED) {
		switch (conn->state) {
		case STATE_SENDING_HEADER:
			dlog(LOG_DEBUG, "Send 200 header\n");
			rc = connection_send_data(conn);
			if (rc > 0)
				conn->state = STATE_HEADER_SENT;
			break;
		case STATE_HEADER_SENT:
			if (conn->res_type == RESOURCE_TYPE_STATIC)
				conn->state = connection_send_static(conn);
			else if (conn->res_type == RESOURCE_TYPE_DYNAMIC)
				conn->state = STATE_SENDING_DATA;
			break;
		case STATE_SENDING_DATA:
			if (conn->res_type == RESOURCE_TYPE_STATIC)
				conn->state = connection_send_static(conn);
			else if (conn->res_type == RESOURCE_TYPE_DYNAMIC)
				connection_send_dynamic(conn);
			break;
		case STATE_SENDING_404:
			rc = connection_send_data(conn);
			dlog(LOG_DEBUG, "Send 404 header\n");
			if (rc > 0)
				conn->state = STATE_404_SENT;
			break;
		case STATE_DATA_SENT:
			connection_remove(conn);
			break;
		case STATE_404_SENT:
			connection_remove(conn);
			break;
		default:
			break;
		}
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	if (event & EPOLLIN)
		handle_input(conn);
	if (event & EPOLLOUT)
		handle_output(conn);
}

int main(void)
{
	int rc;

	/* TODO: Initialize asynchronous operations. */
	rc = io_setup(MAX_EVENTS, &ctx);
	DIE(rc < 0, "io_setup");
	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");
	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, -1);
	DIE(listenfd < 0, "tcp_create_listener");
	/* TODO: Add server socket to epoll object*/
	w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");
	/* Uncomment the following line for debugging. */
	dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* TODO: Wait for events. */
		rc = w_epoll_wait_infinite(epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");
		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		if (rev.data.fd == listenfd) {
			dlog(LOG_DEBUG, "New connection\n");
			if (rev.events & EPOLLIN)
				handle_new_connection();
		} else {
			dlog(LOG_DEBUG, "Handle connection\n");
			handle_client(rev.events, (struct connection *)(rev.data.ptr));
		}
	}
	close(listenfd);
	close(epollfd);
	io_destroy(ctx);
	return 0;
}
