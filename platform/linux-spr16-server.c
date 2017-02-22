/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * TODO: optimize client lookup. sort by msg frequency,
 * or hashmap? that is the question. support multi client regions, mouse, etc.
 *
 * thorough tests would be nice too.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include "../protocol/spr16.h"

extern sig_atomic_t g_mute_input; /* don't forward input if muted */
extern sig_atomic_t g_unmute_input;

/* TODO remove stupid drm hack here for sync msg */
#include "linux-drm.h"
extern struct drm_state g_state;

extern void load_linux_input_drivers(struct input_device **device_list,
		int epoll_fd, int stdin_mode, int evdev);

/*extern void x86_sse2_xmmcpy_256(char *dest, char *src, unsigned int count);
extern void x86_sse2_xmmcpy_512(char *dest, char *src, unsigned int count);
extern void x86_sse2_xmmcpy_1024(char *dest, char *src, unsigned int count);(*/

#define MAX_EPOLL 30
#define MAX_ACCEPT 5

#define STRERR strerror(errno)

struct client
{
	struct spr16 sprite;
	struct client *next;
	int handshaking;
	int connected;
	int socket;
};

struct client *g_focused_client;
struct client *g_clients;
struct client *g_dispatch_client; /* set to null, unless dispatching msgs */
uint16_t g_width;
uint16_t g_height;
uint16_t g_bpp;

int g_epoll_fd;
int g_soft_cursor; /* TODO */


struct input_device *g_input_devices;

static struct client *spr16_server_getclient(int fd)
{
	struct client *cl;
	cl = g_clients;
	while (cl)
	{
		if (cl->socket == fd)
			break;
		cl = cl->next;
	}
	return cl;
}

static int spr16_server_addclient(int fd)
{
	struct epoll_event ev;
	struct client *cl;
	memset(&ev, 0, sizeof(ev));
	if (spr16_server_getclient(fd)) {
		errno = EEXIST;
		return -1;
	}

	cl = malloc(sizeof(*cl));
	if (cl == NULL) {
		return -1;
	}
	memset(cl, 0, sizeof(*cl));
	cl->sprite.shmem.fd = -1;

	ev.events = EPOLLIN;
	ev.data.fd = fd;
	if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev)) {
		printf("epoll_ctl(add): %s\n", STRERR);
		goto err;
	}

	/* send server info to client */
	if (spr16_server_servinfo(fd)) {
		epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		goto err;
	}
	cl->connected = 0;
	cl->socket = fd;
	cl->next   = g_clients;
	g_clients  = cl;

	printf("client added to server\n\n\n");
	return 0;
err:
	free(cl);
	return -1;

}

static int spr16_server_freeclient(struct client *c)
{
	int ret = 0;
	if (c->sprite.shmem.size) {
		close(c->sprite.shmem.fd);
		if (munmap(c->sprite.shmem.addr, c->sprite.shmem.size)) {
			printf("unable to unmap shared memory: %p, %d: %s\n",
				(void *)c->sprite.shmem.addr,
				c->sprite.shmem.size, STRERR);
			ret = -1;
		}
	}
	free(c);
	return ret;
}

static int spr16_server_removeclient(int fd)
{
	struct client *prev, *cl;

	prev = NULL;
	cl = g_clients;
	while (cl)
	{
		if (cl->socket == fd)
			break;
		prev = cl;
		cl = cl->next;
	}
	if (cl == NULL) {
		epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		errno = ESRCH;
		close(fd);
		return -1;
	}
	if (prev == NULL)
		g_clients = NULL;
	else
		prev->next = cl->next;

	if (g_focused_client == cl)
		g_focused_client = NULL;

	if (epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL)) {
		printf("epoll_ctl(del): %s\n", STRERR);
		free(cl);
		close(fd);
		return -1;
	}

	spr16_send_ack(fd, SPRITE_NACK, SPRITENACK_DISCONNECT);
	close(fd);
	if (spr16_server_freeclient(cl))
		return -1;
	return 0;
}

int spr16_server_servinfo(int fd)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_servinfo data;
	memset(&hdr, 0, sizeof(hdr));
	memset(&data, 0, sizeof(data));

	hdr.type    = SPRITEMSG_SERVINFO;
	data.width  = g_width;
	data.height = g_height;
	data.bpp    = g_bpp;
again:
	if (spr16_write_msg(fd, &hdr, &data, sizeof(data))) {
		if (errno == EAGAIN)
			goto again;
		return -1;
	}
	return 0;
}

int spr16_server_register_sprite(int fd, struct spr16_msgdata_register_sprite *reg)
{
	struct client *cl = spr16_server_getclient(fd);
	printf("server got register_sprite\n");
	if (cl == NULL)
		return -1;

	/* register happens only once */
	if (cl->handshaking || cl->connected) {
		printf("bad client\n");
		return -1;
	}
	if (reg->width > g_width || !reg->width) {
		printf("bad width\n");
		spr16_send_ack(fd, SPRITE_NACK, SPRITENACK_WIDTH);
		return -1;
	}
	if (reg->height > g_height || !reg->height) {
		printf("bad height\n");
		spr16_send_ack(fd, SPRITE_NACK, SPRITENACK_HEIGHT);
		return -1;
	}
	if (reg->bpp > g_bpp || reg->bpp < 8) {
		printf("bad bpp\n");
		spr16_send_ack(fd, SPRITE_NACK, SPRITENACK_BPP);
		return -1;
	}
	cl->handshaking = 1;
	cl->sprite.bpp = reg->bpp;
	cl->sprite.width = reg->width;
	cl->sprite.height = reg->height;
	cl->sprite.shmem.size = (reg->bpp/8) * reg->width * reg->height;
	if (spr16_send_ack(fd, SPRITE_ACK, SPRITEACK_SEND_DESCRIPTOR)) {
		printf("send_descriptor ack failed\n");
		return -1;
	}
	return 0;
}

static int open_log(char *socketname)
{
	char path[MAX_SYSTEMPATH];
	int fd;
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
	snprintf(path, sizeof(path), "%s/%s", SPRITE_LOGPATH, socketname);
	fd = open(path, O_WRONLY|O_CLOEXEC|O_CREAT|O_TRUNC, 0755);
	if (fd == -1) {
		printf("open log: %s\n", STRERR);
		return -1;
	}
	if (dup2(fd, STDOUT_FILENO) != STDOUT_FILENO
			|| dup2(fd, STDERR_FILENO) != STDERR_FILENO) {
		printf("dup2: %s\n", STRERR);
		return -1;
	}
	return 0;
}

static int server_create_socket()
{
	int sock;
	struct epoll_event ev;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));

	if (open_log("tty1"))
		return 0;

	addr.sun_family = AF_UNIX;
	/* TODO detect current vt*/
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/tty1", SPRITE_SOCKPATH);
	/* TODO check perms sticky bit on dir, etc */
	sock = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
	if (sock == -1) {
		printf("socket: %s\n", STRERR);
		return -1;
	}
	unlink(addr.sun_path);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		printf("bind: %s\n", STRERR);
		close(sock);
		return -1;
	}
	/* TODO set the group and remove other permission */
	chmod(addr.sun_path, 0777);
	if (listen(sock, MAX_ACCEPT)) {
		printf("listen: %s\n", STRERR);
		close(sock);
		return -1;
	}
	/* create epoll fd and add listener socket*/
	g_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (g_epoll_fd == -1) {
		printf("epoll_create1: %s\n", STRERR);
		close(sock);
		return -1;
	}

	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = sock;
	if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, sock, &ev)) {
		printf("epoll_ctl(add): %s\n", STRERR);
		close(sock);
		close(g_epoll_fd);
		g_epoll_fd = -1;
	}
	return sock;
}

int spr16_server_init(uint16_t width, uint16_t height, uint16_t bpp)
{
	g_input_devices = NULL;
	g_clients = NULL;
	g_dispatch_client = NULL;
	g_focused_client = NULL;
	g_epoll_fd = -1;
	g_width = width;
	g_height = height;
	g_bpp = bpp;
	return server_create_socket();
}


int spr16_server_init_input()
{
	load_linux_input_drivers(&g_input_devices, g_epoll_fd, 0, 1);
	return 0;
}

static int flush_all_devices()
{
	struct input_device *device;
	int ret = 0;
	for (device = g_input_devices; device; device = device->next)
	{
		if (device->fd == -1)
			continue;
		if (device->func_flush(device)) {
			printf("device %s flush failed\n", device->name);
			ret = -1;
		}
	}
	return ret;
}

/*
 *  find the input fd and translate events,
 *
 *  -1 error
 *  0 not an input event
 *  1 was an input event
 */
static int input_event(int evfd)
{
	struct input_device *device;
	int cl_fd;

	if (g_focused_client == NULL) {
		g_focused_client = g_clients;
	}
	cl_fd = g_focused_client ? g_focused_client->socket : -1;

	if (g_unmute_input) {
		g_unmute_input = 0;
		g_mute_input = 0;
		if (flush_all_devices())
			return -1;
	}

	for (device = g_input_devices; device; device = device->next)
	{
		if (evfd == device->fd) {
			if (g_mute_input) {
				if (device->func_read(device, -1)) {
					return -1;
				}
			}
			else {
				if (device->func_read(device, cl_fd)) {
					if (errno == EPIPE) {
						/* client pipe broke */
						spr16_server_removeclient(cl_fd);
						return 1;
					}
					return -1;
				}
			}
			return 1;
		}
	}
	return 0;
}

static int spr16_accept_connection(struct epoll_event *ev)
{
	if (ev->events & EPOLLHUP) {
		printf("listener HUP\n");
		return -1;
	}
	if (ev->events & EPOLLERR) {
		printf("listener ERR\n");
		return -1;
	}
	if (ev->events & EPOLLIN) {
		int newsock;
		struct sockaddr_un addr;
		socklen_t addrlen = sizeof(addr);
		memset(&addr, 0, sizeof(addr));
		newsock = accept4(ev->data.fd, (struct sockaddr *)&addr,
				&addrlen, SOCK_NONBLOCK|SOCK_CLOEXEC);
		if (newsock == -1) {
			printf("accept4: %s\n", STRERR);
			return -1;
		}
		/* TODO we should rate limit this per uid, and timeout drop,
		 * also add a handshake timeout */
		if (spr16_server_addclient(newsock)) {
			close(newsock);
			return -1;
		}
	}

	return 0;
}

static int spr16_server_open_memfd(struct client *cl)
{
	char *addr;
	addr = mmap(0, cl->sprite.shmem.size, PROT_READ,
			MAP_PRIVATE, cl->sprite.shmem.fd, 0);
	if (addr == MAP_FAILED || addr == NULL) {
		printf("mmap error: %s\n", strerror(errno));
		return -1;
	}

	cl->sprite.shmem.addr = addr;
	return 0;
}

/* receive and validate shared memory */
int spr16_server_handshake(struct client *cl)
{
	int fd;
	if (afunix_recv_fd(cl->socket, &fd)) {
		printf("recv descriptor fail\n");
		return -1;
	}
	cl->sprite.shmem.fd = fd;
	if (spr16_server_open_memfd(cl)) {
		spr16_send_ack(fd, SPRITE_NACK, SPRITENACK_SHMEM);
		return -1;
	}
	if (spr16_send_ack(cl->socket, SPRITE_ACK, SPRITEACK_ESTABLISHED)) {
		printf("established ack failed\n");
		return -1;
	}
	return 0;
}

/*
 *
 * TODO arch specific ops to optimize sprite synchronization. sse, neon, etc
 * alpha blending is probably best left to gpu-land, but could also be handled this way.
 *
 * could add a mode that sends pixels over af_unix, for systems without memfd,
 * or any other means of buffer sharing.
 *
 */
/* TODO decouple linux-DRM and spr16
 * this is a big ugly hack right now
 * */
int spr16_server_sync(struct spr16_msgdata_sync *region)
{
	const uint16_t bpp = 32;
	const uint32_t weight = bpp/8;
	struct client *cl;
	uint16_t i;
	uint32_t xoff;
	uint32_t xadj;
	uint32_t width;
	uint32_t count;

	/*
	 * TODO some way to handle multiple displays, for now consider
	 * out of bounds syncs to be errors
	 */
	if (region->x + region->width > g_width
			|| region->y + region->height > g_height) {
		printf("bad sync parameters\n");
		return -1;
	}

	cl = g_dispatch_client;
	if (cl == NULL) {
		printf("null dispatch?\n");
		return -1;
	}
	if (!g_state.sfb || !g_state.sfb->addr || !cl->sprite.shmem.addr) {
		printf("bad ptr\n");
		return -1;
	}

	/* align sync rectangle to grid
	 * 16/32 pixel blocks are always nicely aligned for 8, 16, 32 bpp
	 * 32 pixel grid @ 32bpp = 128byte == full xmm line,
	 *
	 * but is no good @24bpp
	 * fix whenever we get there, for now 24bpp will be left unsupported
	 *
	 */
	xoff  = (region->x - (region->x % 16));
	xadj  = region->x - xoff;
	width = region->width + xadj;
	if (width%16)
		width = (width + (16 - width%16));

	if (xoff+width > g_state.sfb->width) {
		printf("client sent bad sync\n");
		return -1;
	}
	/* convert to bytes */
	width *= weight;
	xoff  *= weight;
	/* TODO apply these newly quantized syncs to a struct that optimizes
	 * rectangle copy jobs for vblank period, don't forget that overly large
	 * grid sizes do more harm than good on low-end hardware
	 * one strategy may be to to keep 1:1 with optimal copy size or so
	 * 32px xmm, 64px ymm, 128px zmm, @ 32bpp
	 * TODO make sure sprite has correct alignment following mmap */
#define BLKSZ 64
	count = width/BLKSZ;
	if (width%BLKSZ) {
		printf("align: %d\n", width%BLKSZ);
		return -1;
	}
	for (i = 0; i < region->height; ++i)
	{
		const uint32_t svoff = (((region->y + i)*g_width)*weight)+xoff;
		const uint32_t cloff = (((region->y + i)*cl->sprite.width)*weight)+xoff;
		uint16_t z;
		for (z = 0; z < count; ++z) {
			memcpy((g_state.sfb->addr + svoff) + (z * BLKSZ),
				(cl->sprite.shmem.addr + cloff) + (z * BLKSZ), BLKSZ);
		}
		/*x86_sse2_xmmcpy_256(g_state.sfb->addr + svoff,
				    cl->sprite.shmem.addr + cloff,
				    count);*/
	}
	return 0;
}

int spr16_server_update(int listen_fd)
{
	struct epoll_event events[MAX_EPOLL];
	int i;
	int evcount;
	evcount = epoll_wait(g_epoll_fd, events, MAX_EPOLL, -1);
	if (evcount == -1) {
		if (errno == EINTR) {
			return 0;
		}
		printf("epoll_wait: %s\n", STRERR);
		return -1;
	}
	for (i = 0; i < evcount; ++i)
	{
		int evfd = events[i].data.fd;
		int r = input_event(evfd);
		if (r == 1) { /* input event */
			continue;
		}
		else if (r == -1) { /* input error */
			printf("input error\n");
			return -1;
		}
		else if (evfd == listen_fd) { /* new client connecting */
			if (spr16_accept_connection(&events[i])) {
				printf("error on listening socket\n");
				/*goto failure; TODO handle this*/
				continue;
			}
		}
		else { /* established client sending data */
			struct client *cl;
			char *msgbuf;
			uint32_t msglen;
			cl = spr16_server_getclient(evfd);
			if (cl == NULL) {
				printf("client missing\n");
				close(evfd);
				continue;
			}
			if (cl->handshaking) {
				if (spr16_server_handshake(cl)) {
					printf("handshake failed\n");
					spr16_server_removeclient(evfd);
					continue;
				}
				cl->connected = 1;
				cl->handshaking = 0;
				/* switch focus to new client */
				if (g_focused_client == NULL)
					g_focused_client = cl;

				continue;
			}
			/* read and dispatch */
			msgbuf = spr16_read_msgs(evfd, &msglen);
			if (msgbuf == NULL) {
				printf("read_msgs: %s\n", STRERR);
				spr16_server_removeclient(evfd);
				continue;
			}
			g_dispatch_client = cl;
			if (spr16_dispatch_msgs(evfd, msgbuf, msglen)) {
				g_dispatch_client = NULL;
				printf("dispatch_msgs: %s\n", STRERR);
				spr16_server_removeclient(evfd);
				continue;
			}
			g_dispatch_client = NULL;
		}
	}

	return 0;
}

int spr16_server_shutdown(int listen_fd)
{
	printf("--- server shutdown ---\n");
	while (g_clients)
	{
		struct client *tmp = g_clients->next;
		spr16_send_ack(g_clients->socket, SPRITE_NACK, SPRITENACK_DISCONNECT);
		spr16_server_freeclient(g_clients);
		g_clients = tmp;
	}
	close(listen_fd);
	close(g_epoll_fd);
	g_epoll_fd = -1;
	g_clients = NULL;
	g_focused_client = NULL;
	return 0;
}

