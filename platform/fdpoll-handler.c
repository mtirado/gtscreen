/* Copyright (C) 2017 Michael R. Tirado <mtirado418@gmail.com> -- GPLv3+
 *
 * This program is libre software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. You should have
 * received a copy of the GNU General Public License version 3
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * note: adding an fd handler will set the fd as nonblocking.
 *
 *       callbacks should read until EAGAIN, and read after FDPOLLHUP to make sure
 *       in flight data is received. shutdown/close causes FDPOLLHUP, and EPOLLIN
 *       is not raised for packetized(dgram/seqpacket) afunix types if data is still
 *       in flight, i think it is one event per write. afunix stream & pipes seem ok.
 *
 * also note: do not remove fd's from within a callback, use the remove return code!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "fdpoll-handler.h"

/*
 * TODO poll support, make a linux ifdef that can choose epoll or poll at runtime.
 * generic poll version for non-linux platforms hurd, bsd, etc. it will need very fast
 * lookups to get callback data on par with epoll, linear 1:1 fd keyed array would be
 * fastest but it will have to be large enough or capped to fit all fd numbers
 */

struct fdpoll_handler *fdpoll_handler_create(unsigned int max, int cloexec)
{
	struct fdpoll_handler *self;

	self = malloc(sizeof(struct fdpoll_handler));
	if (!self) {
		return NULL;
	}
	memset(self, 0, sizeof(struct fdpoll_handler));
	self->max = max;

	self->fdpoll_fd = epoll_create1(cloexec ? EPOLL_CLOEXEC : 0);
	if (self->fdpoll_fd == -1) {
		free(self);
		fprintf(stderr, "epoll_create1: %s\n", strerror(errno));
		return NULL;
	}
	return self;

}

static struct fdpoll_node *fdpoll_handler_find_node(struct fdpoll_handler *self, int fd)
{
	struct fdpoll_node *node = self->list;

	while (node)
	{
		if (node->fd == fd)
			break;
		node = node->next;
	}
	return node;
}

/*static void fdpoll_print_list(struct fdpoll_handler *self)
{
	struct fdpoll_node *node = self->list;

	fprintf(stderr, "fdpoll -- [");
	while (node)
	{
		fprintf(stderr, " %d ", node->fd);
		if (node->next != NULL)
			fprintf(stderr, "|");
		node = node->next;
	}
	fprintf(stderr, "]\n");
}*/

int fdpoll_handler_add(struct fdpoll_handler *self,
		      int fd,
		      uint32_t fdpoll_flags,
		      fdpoll_handler_cb cb,
		      void *user_data)
{
	struct epoll_event ev;
	struct fdpoll_node *node = NULL;
	int flags;

	if (self->count >= MAX_FDPOLL_HANDLER || self->count >= self->max
			|| fd < 0 || cb == NULL)
		return -1;

	if (fdpoll_handler_find_node(self, fd))
		return -1;


	node = malloc(sizeof(struct fdpoll_node));
	if (node == NULL)
		return -1;
	memset(node, 0, sizeof(struct fdpoll_node));

	memset(&ev, 0, sizeof(ev));
	ev.events = fdpoll_flags;
	ev.data.ptr = node;
	if (epoll_ctl(self->fdpoll_fd, EPOLL_CTL_ADD, fd, &ev)) {
		fprintf(stderr, "epoll_ctl(add): %s\n", strerror(errno));
		goto free_fail;
	}

	/*  all fd's must be non-blocking */
	flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) {
		fprintf(stderr, "fcntl(getfl): %s\n", strerror(errno));
		if (epoll_ctl(self->fdpoll_fd, EPOLL_CTL_DEL, fd, NULL)) {
			fprintf(stderr, "epoll_ctl_del: %s\n", strerror(errno));
		}
		goto free_fail;
	}
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags)) {
		fprintf(stderr, "fcntl(setfl): %s\n", strerror(errno));
		if (epoll_ctl(self->fdpoll_fd, EPOLL_CTL_DEL, fd, NULL)) {
			fprintf(stderr, "epoll_ctl_del: %s\n", strerror(errno));
		}
		goto free_fail;
	}

	node->fd = fd;
	node->cb = cb;
	node->user_data = user_data;
	node->next = self->list;
	self->list = node;
	++self->count;
	return 0;

free_fail:
	free(node);
	return -1;
}

int fdpoll_handler_remove(struct fdpoll_handler *self, int fd)
{
	struct fdpoll_node *node = self->list;
	struct fdpoll_node **trail = &self->list;

	errno = 0;

	if (self->count == 0) {
		errno = EOVERFLOW;
		return -1;
	}
	--self->count;

	while (node)
	{
		if (node->fd == fd) {
			*trail = node->next;
			free(node);
			break;
		}
		trail = &node->next;
		node = node->next;
	}

	if (node == NULL) {
		errno = ESRCH;
		return -1;
	}

	if (epoll_ctl(self->fdpoll_fd, EPOLL_CTL_DEL, fd, NULL)) {
		/* errno ENOENT if it can't find fd */
		fprintf(stderr, "epoll_ctl_del: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int fdpoll_handler_poll(struct fdpoll_handler *self, int timeout)
{
	struct epoll_event events[MAX_FDPOLL_HANDLER];
	int remove[MAX_FDPOLL_HANDLER];
	int num_rmed = 0;
	int i;
	int evcount;

	evcount = epoll_wait(self->fdpoll_fd, events, MAX_FDPOLL_HANDLER, timeout);
	if (evcount < 0 || evcount >= MAX_FDPOLL_HANDLER) {
		if (errno == EINTR) {
			return 0;
		}
		fprintf(stderr, "fdpoll_wait: %s\n", strerror(errno));
		return -1;
	}

	for (i = 0; i < evcount; ++i)
	{
		struct fdpoll_node *data = events[i].data.ptr;
		int event_flags = events[i].events;
		int r;

		if (data == NULL || data->cb == NULL) {
			fprintf(stderr, "null handler pointer\n");
			return -1;
		}
		r = data->cb(data->fd, event_flags, data->user_data);
		if (r == FDPOLL_HANDLER_REMOVE || event_flags & (EPOLLHUP | EPOLLERR)) {
			if (num_rmed >= (int)self->max)
				return -1;
			remove[num_rmed] = data->fd;
			++num_rmed;
		}
		else if (r != FDPOLL_HANDLER_OK) {
			fprintf(stderr, "bad fdpoll handler return code: %d\n", r);
			return -1;
		}
	}

	/* remove after we handle all events that may reference it */
	for (i = 0; i < num_rmed; ++i)
	{
		printf("queued for removal: %d\n", remove[i]);
		if (fdpoll_handler_remove(self, remove[i])) {
			fprintf(stderr, "fdpoll_handler_remove, problem with: %d\n",
					remove[i]);
		}
	}
	return 0;
}

void fdpoll_handler_destroy(struct fdpoll_handler *self)
{
	struct fdpoll_node *node = self->list;
	struct fdpoll_node *freeme = NULL;

	while (node)
	{
		freeme = node;
		node = node->next;
		free(freeme);
	}
	close(self->fdpoll_fd);
	free(self);
}
