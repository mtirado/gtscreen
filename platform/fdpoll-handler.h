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
 * TODO add poll/select support for systems without epoll
 */

#ifndef FDPOLL_HANDLER_H__
#define FDPOLL_HANDLER_H__

#include <sys/epoll.h>
#include <sys/poll.h>

#define MAX_FDPOLL_HANDLER 2048

/* fdpoll handler return codes */
enum {
	FDPOLL_HANDLER_OK = 0,
	FDPOLL_HANDLER_REMOVE
};

#define FDPOLLIN     POLLIN
#define FDPOLLPRI    POLLPRI
#define FDPOLLOUT    POLLOUT
#define FDPOLLRDNORM POLLRDNORM
#define FDPOLLRDBAND POLLRDBAND
#define FDPOLLWRNORM POLLWRNORM
#define FDPOLLWRBAND POLLWRBAND
#define FDPOLLERR    POLLERR
#define FDPOLLHUP    POLLHUP

/* linux extensions
 * #define FDPOLLRDHUP 0x2000
 * #define FDPOLLMSG 0x400
 */

typedef int (*fdpoll_handler_cb)(int fd, int fdpoll_flags, void *user_data);
struct fdpoll_node {
	struct fdpoll_node *next;
	void *user_data;
	fdpoll_handler_cb cb;
	int fd;
};

/* TODO compact memory use with dynamic resizing */
struct fdpoll_handler {
	int fdpoll_fd;
	unsigned int count;
	unsigned int max;
	struct fdpoll_node *list;
};

struct fdpoll_handler *fdpoll_handler_create(unsigned int max, int cloexec);
/* note: this will set fd to NONBLOCKING */
int fdpoll_handler_add(struct fdpoll_handler *self, int fd, uint32_t poll_flags,
			fdpoll_handler_cb cb, void *user_data);
int fdpoll_handler_remove(struct fdpoll_handler *self, int fd);
int fdpoll_handler_poll(struct fdpoll_handler *self, int timeout);

/*  example callback
int callback(int fd, int event_flags, void *user_data)
{
	(void)user_data;
	if (event_flags & FDPOLLIN) {
		if (read_fd(fd)) {
			return FDPOLL_HANDLER_REMOVE;
		}
	}
	if (event_flags & FDPOLLERR) {
		if (read_fd(fd)) {
			return FDPOLL_HANDLER_REMOVE;
		}
	}
	if (event_flags & FDPOLLOUT) {
		if (write_fd(fd)) {
			return FDPOLL_HANDLER_REMOVE;
		}
	}
	if (event_flags & FDPOLLHUP) {
		if (read_fd(fd)) {
			return FDPOLL_HANDLER_REMOVE;
		}
	}

	return FDPOLL_HANDLER_OK;
}
*/
#endif
