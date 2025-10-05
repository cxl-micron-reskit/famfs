// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

#define _GNU_SOURCE
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include "../mongoose/mongoose.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <libgen.h>   /* dirname() */
#include <sys/param.h> /* MIN()/MAX() */

#include "famfs_log.h"
#include "famfs_fused.h"

static pthread_t diag_thread;
static volatile int diag_shutdown_requested = 0;
static int diag_server_running = 0;
static char *sock_path = NULL;

/*
 * Handle a parsed HTTP request and replies
 */
static void famfs_dispatch_http(
	struct mg_connection *c, struct mg_http_message *hm)
{
	famfs_log(FAMFS_LOG_NOTICE, "%s: uri=%.*s\n", __func__,
		  (int) hm->uri.len, hm->uri.buf);

	if (mg_match(hm->uri, mg_str("/log_level"), NULL)) {
		if (mg_match(hm->method, mg_str("GET"), NULL)) {
			int log_level = famfs_log_get_level();
			char *meta = "Content-Type: text/plain\r\n"
				"Connection: close\r\n";

			mg_http_reply(c, 200, meta,
				      "%d\n", famfs_log_get_level());
			famfs_log(log_level, "log_level is %d (%s)",
				  log_level, famfs_log_level_string(log_level));
			

		} else if (mg_match(hm->method, mg_str("POST"), NULL) ||
			   mg_match(hm->method, mg_str("PUT"), NULL)) {
			// Body may contain "level=3" or just "3"
			char buf[32] = {0};
			int len = (int) MIN(hm->body.len, sizeof(buf) - 1);
			int new_level;
			int check_level, recheck_level;

			check_level = famfs_log_get_level();
			famfs_log(check_level, "POST/PUT log_level");
			memcpy(buf, hm->body.buf, len);
			buf[len] = 0;

			new_level = atoi(buf);
			famfs_log_set_level(new_level);
			recheck_level = famfs_log_get_level();
			
			if (new_level == recheck_level) {
				char *meta = "Content-Type: text/plain\r\n"
					"Connection: close\r\n";
				mg_http_reply(c, 200, meta,
					      "log level set to %d (from %d)\n",
					      new_level, check_level);
				famfs_log(new_level,
					  "log_level changed %d->%d (%s->%s)\n",
					  check_level, new_level,
					  famfs_log_level_string(check_level),
					  famfs_log_level_string(new_level));
			} else {
				char *meta = "Content-Type: text/plain\r\n"
					"Connection: close\r\n";
					mg_http_reply(c, 200, meta,
					      "Failed to set log level to %d\n",
					      new_level);
			}
		} else {
			mg_http_reply(c, 405,
				      "Connection: close\r\n",
				      "Method Not Allowed\n");
		}
	} else if (mg_match(hm->uri, mg_str("/icache_dump"), NULL)) {
		/* Dump the icache to the syslog */
		int loglevel = famfs_log_get_level();
		extern struct famfs_ctx famfs_context;
		char *meta = "Content-Type: text/yaml\r\nConnection: close\r\n";
		dump_icache(&famfs_context.icache, loglevel);
		mg_http_reply(c, 200, meta,
			      "icache: dumping icache to syslog\n");

	} else if (mg_match(hm->uri, mg_str("/inodes"), NULL)) {
		/* Dummy target */
		mg_http_reply(c, 200,
			      "Content-Type: text/yaml\r\nConnection: close\r\n",
			      "inodes:\n  total: 1500\n  open: 12\n  deleted: 7\n");

	} else {
		char *meta = "Content-Type: text/plain\r\nConnection: close\r\n";
		mg_http_reply(c, 404, meta, "Not Found\n");
	}

	/* Ensure the socket closes after we flush the reply
	 * (avoid endless MG_EV_READ spam) */
	c->is_draining = 1;
}


/*
 * Raw event bridge: turn MG_EV_READ bytes into a parsed HTTP message
 */
static void famfs_unix_bridge(struct mg_connection *c, int ev, void *ev_data)
{
	(void) ev_data;
	if (ev == MG_EV_READ) {
		for (;;) {
			struct mg_http_message hm;
			int n = mg_http_parse((char *) c->recv.buf, c->recv.len, &hm);
			if (n > 0) {
				// got a complete request
				famfs_dispatch_http(c, &hm);
				mg_iobuf_del(&c->recv, 0, (size_t) n);
			} else if (n < 0) {
				// bad request
				mg_http_reply(c, 400,
					      "Connection: close\r\n",
					      "Bad Request\n");
				c->is_draining = 1;
			} else {
				/* n == 0 -> incomplete, do nothing;
				 * wait for more data
				 */
				return;
			}
		}
	} else if (ev == MG_EV_CLOSE) {
		/* optional: log/cleanup */
	}
}


/*
 * Rest server thread
 */
static void *diag_server_thread_fn(void *arg)
{
	(void)arg;
	int listen_fd = -1;
	struct sockaddr_un sa;
	struct mg_mgr mgr;

	if (!sock_path || !*sock_path) return NULL;
	unlink(sock_path);

	listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		famfs_log(FAMFS_LOG_ERR,
			  "%s: socket failed AF_UNIX (errno=%d)\n",
			  __func__, errno);
		perror("socket");
		return NULL;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, sock_path, sizeof(sa.sun_path) - 1);

	if (bind(listen_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		famfs_log(FAMFS_LOG_ERR, "%s: bind failed (errno=%d)\n",
			  __func__, errno);
		perror("bind");
		close(listen_fd);
		return NULL;
	}

	chmod(sock_path, 0660);
	if (listen(listen_fd, 64) < 0) {
		famfs_log(FAMFS_LOG_ERR,
			  "%s: socket failed AF_UNIX (errno=%d)\n",
			  __func__, errno);
		perror("listen");
		close(listen_fd);
		unlink(sock_path);
		return NULL;
	}

	mg_mgr_init(&mgr);
	diag_server_running = 1;
	famfs_log(FAMFS_LOG_NOTICE, "Listening on unix:%s\n", sock_path);

	while (!diag_shutdown_requested) {
		struct timeval tv = {0, 100000};
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(listen_fd, &fds);
		int r = select(listen_fd + 1, &fds, NULL, NULL, &tv);

		if (r > 0 && FD_ISSET(listen_fd, &fds)) {
			struct mg_connection *c;

			int client_fd = accept(listen_fd, NULL, NULL);
			if (client_fd >= 0) {
				c = mg_wrapfd(&mgr, client_fd,
					      famfs_unix_bridge, NULL);
				if (!c)
					close(client_fd);
			}
		}

		mg_mgr_poll(&mgr, 10);
	}

	mg_mgr_free(&mgr);
	close(listen_fd);
	unlink(sock_path);
	diag_server_running = 0;
	return NULL;
}


/*
 * build_sock_path - return "<dirname(path)>/sock"
 *
 * Caller must free() the returned string.
 */
char *sock_path_from_shadow_root(const char *path)
{
	char *path_copy, *dir, *result;
	size_t len;

	if (!path)
		return NULL;

	/* dirname() may modify its input */
	path_copy = strdup(path);
	if (!path_copy)
		return NULL;

	dir = dirname(path_copy);
	if (!dir) {
		free(path_copy);
		return NULL;
	}

	/* Allocate space for "<dir>/sock" + NUL */
	len = strlen(dir) + 1 + strlen("sock") + 1;
	result = malloc(len);
	if (!result) {
		free(path_copy);
		return NULL;
	}

	snprintf(result, len, "%s/%s", dir, "sock");

	free(path_copy);
	return result;
}

/*
 * Start and stop rest server
 */
void famfs_diag_server_start(const char *shadow_root)
{
	if (diag_server_running)
		return;
	free(sock_path);
	sock_path = sock_path_from_shadow_root(shadow_root);
	famfs_log(FAMFS_LOG_NOTICE, "Starting rest server on %s\n", sock_path);
	diag_shutdown_requested = 0;
	pthread_create(&diag_thread, NULL, diag_server_thread_fn, NULL);
}

void famfs_diag_server_stop(void)
{
	famfs_log(FAMFS_LOG_NOTICE, "Stopping rest server\n");
	if (!diag_server_running)
		return;
	diag_shutdown_requested = 1;
	pthread_join(diag_thread, NULL);
	free(sock_path);
	sock_path = NULL;
}
