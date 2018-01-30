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
 */

#define _GNU_SOURCE
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <stdlib.h>
#include "../../spr16.h"

#define STRERR strerror(errno)
#define MAX_EPOLL 10

input_handler g_input_func;
input_surface_handler g_input_surface_func;
servinfo_handler g_servinfo_func;

/* TODO we probably want a client context */
struct spr16 g_sprite;
struct spr16_msgdata_servinfo g_servinfo;
int g_socket;
int g_wait_vsync;

struct epoll_event g_events[MAX_EPOLL];
struct epoll_event g_ev;
int g_epoll_fd;
int g_handshaking;

struct spr16_msgdata_servinfo *spr16_client_get_servinfo()
{
	return &g_servinfo;
}
struct spr16 *spr16_client_get_sprite()
{
	return &g_sprite;
}

int spr16_client_init()
{
	g_input_func = NULL;
	g_input_surface_func = NULL;
	g_servinfo_func = NULL;
	g_epoll_fd = -1;
	g_socket = -1;
	g_handshaking = 1;
	g_wait_vsync = 0;
	memset(&g_servinfo, 0, sizeof(g_servinfo));
	memset(&g_sprite, 0, sizeof(g_sprite));

	return 0;
}

/*
 * returns connected socket
 */
int spr16_client_connect(char *name)
{
	struct sockaddr_un addr;

	if (name == NULL) {
		name = getenv("SPR16_SOCKET");
		if (name == NULL) {
			fprintf(stderr, "using SPR16_SOCKET=%s\n", SPR16_DEFAULT_SOCKET);
			name = SPR16_DEFAULT_SOCKET;
		}
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", SPR16_SOCKPATH, name);
	/* TODO check perms, sticky bit on dir, etc */
	g_socket = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
	if (g_socket == -1)
		return -1;

	if (connect(g_socket, (struct sockaddr *)&addr, sizeof(addr))) {
		fprintf(stderr, "connect(%s): %s\n", addr.sun_path, STRERR);
		return -1;
	}

	g_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (g_epoll_fd == -1) {
		fprintf(stderr, "epoll_create1: %s\n", STRERR);
		close(g_socket);
		return -1;
	}

	memset(&g_ev, 0, sizeof(g_ev));
	g_ev.events = EPOLLIN;
	g_ev.data.fd = g_socket;
	if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_socket, &g_ev)) {
		fprintf(stderr, "epoll_ctl(add): %s\n", STRERR);
		goto failure;
	}
	return g_socket;
failure:
	close(g_epoll_fd);
	close(g_socket);
	return -1;
}

int spr16_client_handshake_start(char *name, uint16_t width, uint16_t height, uint32_t flags)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_register_sprite data;
	uint16_t bpp = 32;
	memset(&hdr, 0, sizeof(hdr));
	memset(&data, 0, sizeof(data));

	/* send register */
	if (width > g_servinfo.width || height > g_servinfo.height) {
		fprintf(stderr, "sprite size(%d, %d) -- server max(%d, %d)\n",
				width,height,g_servinfo.width,g_servinfo.height);
		return -1;
	}
	hdr.type = SPRITEMSG_REGISTER_SPRITE;
	data.flags = flags;
	data.width = width;
	data.height = height;
	data.bpp = bpp;
	snprintf(data.name, SPR16_MAXNAME, "%s", name);
	if (spr16_write_msg(g_socket, &hdr, &data, sizeof(data))) {
		return -1;
	}
	g_sprite.flags = flags;
	g_sprite.width = width;
	g_sprite.height = height;
	g_sprite.bpp = bpp;
	return 0;
}

int spr16_client_servinfo(struct spr16_msgdata_servinfo *sinfo)
{
	/* this info is static, msg is expected only once */
	if (g_servinfo.bpp)
		return -1;
	if (!sinfo->bpp || !sinfo->width || !sinfo->height)
		return -1;

	memcpy(&g_servinfo, sinfo, sizeof(g_servinfo));
	if (!g_servinfo_func)
		return 0;
	return g_servinfo_func(&g_servinfo);
}

int spr16_client_waiting_for_vsync()
{
	return g_wait_vsync;
}

/* it is assumed for now to be a full screen memory region */
int spr16_open_shmem(int fd)
{
	char *addr;
	size_t size = (g_sprite.bpp/8) * g_sprite.width * g_sprite.height;
	addr = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED || addr == NULL) {
		printf("mmap error: %s\n", strerror(errno));
		return -1;
	}
	g_sprite.shmem.addr = addr;
	g_sprite.shmem.size = size;
	g_sprite.shmem.fd = fd;
	return 0;
}

static int recv_fd()
{
	char *msgbuf;
	uint32_t msglen;
	int r;
	int fd = -1;
	int c = 5000;

	/* clear pending messages */
	do {
		msgbuf = spr16_read_msgs(g_socket, &msglen);

	} while(msgbuf || (msgbuf == NULL && errno == EINTR));

	if (errno != EAGAIN) {
		printf("problem clearing pending messages: %s\n", strerror(errno));
		return -1;
	}

	/* tell server to send fd now */
	if (spr16_send_ack(g_socket, SPRITEACK_SEND_FD))
		return -1;

	while (--c > 0)
	{
		r = afunix_recv_fd(g_socket, &fd);
		if (r == -1 && (errno == EINTR || errno == EAGAIN)) {
			usleep(1000);
			continue;
		}
		else if (r == -1) {
			spr16_send_nack(g_socket, SPRITENACK_FD);
			return -1;
		}
		else {
			break;
		}
	}
	if (c <= 0) {
		printf("recv_fd timed out\n");
		spr16_send_nack(g_socket, SPRITENACK_FD);
		return -1;
	}

	if (spr16_open_shmem(fd)) {
		fprintf(stderr, "open_shmem failed\n");
		spr16_send_nack(g_socket, SPRITENACK_SHMEM);
		return -1;
	}
	if (spr16_send_ack(g_socket, SPRITEACK_ESTABLISHED))
		return -1;
	g_handshaking = 0;

	return 0;
}
/* ack / nack */
static int handle_ack(struct spr16_msgdata_ack *ack)
{
	switch (ack->info)
	{
	case SPRITEACK_RECV_FD:
		return recv_fd();
		break;
	case SPRITEACK_ESTABLISHED:
		break;
	case SPRITEACK_SYNC_VSYNC:
		g_wait_vsync = 0;
		break;
	default:
		fprintf(stderr, "unknown ack\n");
		return -1;
	}
	return 0;
}
static int handle_nack(struct spr16_msgdata_ack *nack)
{
	switch (nack->info)
	{
	case SPRITENACK_DISCONNECT:
		fprintf(stderr, "nack: disconnected\n");
		errno = ECONNRESET;
		return -1;
	case SPRITENACK_SHMEM:
		fprintf(stderr, "nack: shared memory error\n");
		break;
	case SPRITENACK_WIDTH:
		fprintf(stderr, "nack: bad width\n");
		break;
	case SPRITENACK_HEIGHT:
		fprintf(stderr, "nack: bad height\n");
		break;
	case SPRITENACK_BPP:
		fprintf(stderr, "nack: bad bpp\n");
		break;
	default:
		fprintf(stderr, "unhandled nack: %d\n", nack->info);
		errno = EPROTO;
		return -1;
	}
	return 0;
}

int spr16_client_ack(struct spr16_msgdata_ack *ack)
{
	if (ack->ack)
		return handle_ack(ack);
	else
		return handle_nack(ack);
}

/* TODO, add flags */
int spr16_client_sync(uint16_t xmin, uint16_t ymin,
		      uint16_t xmax, uint16_t ymax, uint16_t flags)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_sync data;
	if (g_handshaking) {
		return 0;
	}

	hdr.type = SPRITEMSG_SYNC;
	hdr.bits = flags;
	data.xmin = xmin;
	data.ymin = ymin;
	data.xmax = xmax;
	data.ymax = ymax;
	if (spr16_write_msg(g_socket, &hdr, &data, sizeof(data))) {
		return -1;
	}
	if (flags & SPRITESYNC_FLAG_VBLANK)
		g_wait_vsync = 1;
	return 0;
}

int spr16_client_handshake_wait(uint32_t timeout)
{
	uint32_t c = 0;
	/* milliseconds */
	if (timeout < 500)
		timeout = 500;
	while(1)
	{
		if (spr16_client_update(1) && errno != EBADF)
			return -1;
		if (!g_handshaking)
			return 0;
		if (++c > timeout)
			return -1;
	}
}

int spr16_client_shutdown()
{
	memset(&g_servinfo, 0, sizeof(g_servinfo));
	memset(&g_sprite, 0, sizeof(g_sprite));
	close(g_epoll_fd);
	close(g_socket);
	g_socket = -1;
	g_epoll_fd = -1;
	/* TODO unmap sprite memory and free any other state data that might exist */
	return 0;
}

/* TODO */
static int surface_emulate_pointer(struct spr16_msgdata_input_surface *msg)
{
	(void)msg;
	if (!g_input_func)
		return 0;
	return 0;
}

int spr16_client_input_surface(struct spr16_msgdata_input_surface *msg)
{
	if (g_input_surface_func == NULL) {
		return surface_emulate_pointer(msg);
	}
	else {
		return g_input_surface_func(msg);
	}
}

int spr16_client_input(struct spr16_msgdata_input *msg)
{
	if (g_input_func == NULL) {
		return 0;
	}
	else {
		return g_input_func(msg);
	}
}

int spr16_client_set_input_handler(input_handler func)
{
	g_input_func = func;
	return 0;
}

int spr16_client_set_input_surface_handler(input_surface_handler func)
{
	g_input_surface_func = func;
	return 0;
}

int spr16_client_set_servinfo_handler(servinfo_handler func)
{
	g_servinfo_func = func;
	return 0;
}

int spr16_dispatch_client_msgs(char *msgbuf, uint32_t buflen)
{
	struct spr16_msghdr *msghdr;
	char *msgpos, *msgdata;
	int rdpos;
	uint32_t typelen;
	errno = 0;
	rdpos = 0;
	while (rdpos+sizeof(struct spr16_msghdr) < buflen)
	{
		msgpos  = msgbuf+rdpos;
		msghdr  = (struct spr16_msghdr *)msgpos;
		msgdata = msgpos+sizeof(struct spr16_msghdr);
		typelen = get_msghdr_typelen(msghdr);
		if (typelen > SPR16_MAXMSGLEN - sizeof(struct spr16_msghdr)
				|| msgdata+typelen > msgbuf+buflen) {
			errno = EPROTO;
			return -1;
		}
		switch (msghdr->type)
		{
		case SPRITEMSG_SERVINFO:
			if (spr16_client_servinfo(
					(struct spr16_msgdata_servinfo *)msgdata)) {
				fprintf(stderr, "servinfo failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_ACK:
			if (spr16_client_ack((struct spr16_msgdata_ack *)msgdata)) {
				fprintf(stderr, "ack failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_INPUT:
			if (spr16_client_input((struct spr16_msgdata_input *)msgdata)) {
				fprintf(stderr, "input failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_INPUT_SURFACE:
			if (spr16_client_input_surface(
						(struct spr16_msgdata_input_surface *)
						msgdata)) {
				fprintf(stderr, "input_surface failed\n");
				return -1;
			}
			break;
		default:
			errno = EPROTO;
			return -1;
		}
		rdpos += sizeof(struct spr16_msghdr) + typelen;
	}

	return 0;
}

int spr16_client_update(const int poll_timeout)
{
	int i;
	int evcount;
	char *msgbuf;
	uint32_t msglen;
interrupted:
	errno = 0;
	evcount = epoll_wait(g_epoll_fd, g_events, MAX_EPOLL,
			(poll_timeout < 0) ? -1 : poll_timeout);
	if (evcount == -1) {
		if (errno == EINTR) {
			goto interrupted;
		}
		fprintf(stderr, "epoll_wait: %s\n", STRERR);
		return -1;
	}

	for (i = 0; i < evcount; ++i)
	{
		/* TODO use fdpoll */
		if (g_events[i].data.fd == g_socket) {
			msgbuf = spr16_read_msgs(g_socket, &msglen);
			if (msgbuf == NULL) {
				fprintf(stderr, "read_msgs: %s\n", STRERR);
				return -1;
			}
			if (spr16_dispatch_client_msgs(msgbuf, msglen)) {
				fprintf(stderr, "dispatch_client_msgs: %s\n", STRERR);
				return -1;
			}
		}
		else {
			fprintf(stderr, "badf\n");
			errno = EBADF;
			return -1;
		}
	}

	return 0;
}

