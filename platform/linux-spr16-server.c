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
#include "../screen.h"

extern sig_atomic_t g_mute_input; /* don't forward input if muted */
extern sig_atomic_t g_unmute_input;

/* TODO remove stupid drm hack here for sync msg */
#include "linux-drm.h"
extern struct drm_state g_state;

extern void load_linux_input_drivers(struct input_device **device_list,
		int epoll_fd, int stdin_mode, int evdev, input_hotkey hk);

/*extern void x86_sse2_xmmcpy_256(char *dest, char *src, unsigned int count);
extern void x86_sse2_xmmcpy_512(char *dest, char *src, unsigned int count);
extern void x86_sse2_xmmcpy_1024(char *dest, char *src, unsigned int count);(*/

#define MAX_EPOLL 30
#define MAX_ACCEPT 5

#define STRERR strerror(errno)

struct screen *g_main_screen;
struct client *g_pending_clients; /* clients with incomplete handshakes */
struct client *g_dispatch_client; /* null unless dispatching a message */
uint16_t g_width;
uint16_t g_height;
uint16_t g_bpp;

int g_epoll_fd;

struct input_device *g_input_devices;

static struct client *server_remove_pending(int fd)
{
	struct client **trail = &g_pending_clients;
	struct client     *cl =  g_pending_clients;
	while (cl)
	{
		if (cl->socket == fd) {
			*trail = cl->next;
			break;
		}
		trail = &cl->next;
		cl = cl->next;
	}
	return cl;
}

static struct client *server_getclient(int fd)
{
	struct client *cl = NULL;
	struct screen *scrn;
	scrn = g_main_screen;
	while (scrn)
	{
		cl = screen_find_client(scrn, fd);
		if (cl)
			return cl;
		scrn = scrn->next;
	}

	cl = g_pending_clients;
	while (cl)
	{
		if (cl->socket == fd) {
			return cl;
		}
	}
	return NULL;
}
static void server_close_socket(int fd)
{
	epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
	close(fd);
}
static int server_free_client(struct client *c)
{
	int ret = 0;
	server_close_socket(c->socket);
	if (c->sprite.shmem.size) {
		/* TODO should use PROT_WRITE + shared
		 * to force clearing sprites on disconnect
		 * memset(c->sprite.shmem.addr, 0xAA, c->sprite.shmem.size);*/
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
static int server_remove_client(int fd)
{
	struct screen *scrn;
	struct screen **trail;
	struct client *cl;

	errno = 0;
	scrn  =  g_main_screen;
	trail = &g_main_screen;
	while (scrn)
	{
		cl = screen_remove_client(scrn, fd);
		if (cl) {
			spr16_send_ack(fd, SPR16_NACK, SPRITENACK_DISCONNECT);
			if (server_free_client(cl))
				return -1;
			*trail = scrn->next;
			free(scrn); /* FIXME, add multiple clients */
			return 0;
		}
		trail = &scrn->next;
		scrn  =  scrn->next;
	}
	if (server_remove_pending(fd) == NULL) {
		server_close_socket(fd);
		errno = ESRCH;
		return -1;
	}
	return 0;
}

static int server_connect_client(struct client *cl)
{
	struct screen *scrn, *tmp;

	/* hacky right now, only one client per screen */
	scrn = malloc(sizeof(struct screen));
	if (scrn == NULL)
		return -1;
	if (screen_init(scrn))
		goto err;
	if (screen_add_client(scrn, cl))
		goto err;
	/* add to end of screen list */
	tmp = g_main_screen;
	if (tmp) {
		while (tmp->next)
		{
			tmp = tmp->next;
		}
		tmp->next = scrn;
		printf("-- new screen added to existing list --\n");
	}
	else {
		g_main_screen = scrn;
		printf("-- new screen added to empty list --\n");
	}
	cl->connected = 1;
	cl->handshaking = 0;
	return 0;
err:
	free(scrn);
	return -1;
}

static int server_addclient(int fd)
{
	struct epoll_event ev;
	struct client *cl;

	errno = 0;
	memset(&ev, 0, sizeof(ev));
	if (server_getclient(fd)) {
		errno = EEXIST;
		return -1;
	}

	cl = malloc(sizeof(*cl));
	if (cl == NULL) {
		return -1;
	}
	memset(cl, 0, sizeof(*cl));
	cl->sprite.shmem.fd = -1;

	/* send server info to client */
	if (spr16_server_servinfo(fd)) {
		goto err;
	}

	ev.events = EPOLLIN;
	ev.data.fd = fd;
	if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev)) {
		printf("epoll_ctl(add): %s\n", STRERR);
		goto err;
	}

	cl->connected = 0;
	cl->socket = fd;
	cl->next = g_pending_clients;
	g_pending_clients = cl;

	printf("client(%d)added to server\n", fd);
	return 0;
err:
	free(cl);
	return -1;
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
	struct client *cl = server_getclient(fd);
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
		spr16_send_ack(fd, SPR16_NACK, SPRITENACK_WIDTH);
		return -1;
	}
	if (reg->height > g_height || !reg->height) {
		printf("bad height\n");
		spr16_send_ack(fd, SPR16_NACK, SPRITENACK_HEIGHT);
		return -1;
	}
	if (reg->bpp > g_bpp || reg->bpp < 8) {
		printf("bad bpp\n");
		spr16_send_ack(fd, SPR16_NACK, SPRITENACK_BPP);
		return -1;
	}
	cl->handshaking = 1;
	cl->sprite.bpp = reg->bpp;
	cl->sprite.width = reg->width;
	cl->sprite.height = reg->height;
	cl->sprite.shmem.size = (reg->bpp/8) * reg->width * reg->height;
	if (spr16_send_ack(fd, SPR16_ACK, SPRITEACK_SEND_DESCRIPTOR)) {
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
	snprintf(path, sizeof(path), "%s/%s", SPR16_LOGPATH, socketname);
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
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/tty1", SPR16_SOCKPATH);
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
		return -1;
	}
	return sock;
}

int spr16_server_init(uint16_t width, uint16_t height, uint16_t bpp)
{
	g_main_screen = NULL;
	g_input_devices = NULL;
	g_dispatch_client = NULL;
	g_pending_clients = NULL;
	g_epoll_fd = -1;
	g_width = width;
	g_height = height;
	g_bpp = bpp;
	return server_create_socket();
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

	if (!g_main_screen) {
		cl_fd = -1;
	}
	else {
		if (g_main_screen->focused_client == NULL) {
			g_main_screen->focused_client = g_main_screen->clients;
		}
		cl_fd = g_main_screen->focused_client
			? g_main_screen->focused_client->socket : -1;
	}

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
				if (device->func_transceive(device, -1)) {
					return -1;
				}
			}
			else {
				if (device->func_transceive(device, cl_fd)) {
					if (errno == EPIPE) {
						/* client pipe broke */
						server_remove_client(cl_fd);
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
		if (server_addclient(newsock)) {
			printf("server_addclient(%d) failed\n", newsock);
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
		spr16_send_ack(fd, SPR16_NACK, SPRITENACK_SHMEM);
		return -1;
	}
	if (spr16_send_ack(cl->socket, SPR16_ACK, SPRITEACK_ESTABLISHED)) {
		printf("established ack failed\n");
		return -1;
	}
	return 0;
}

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
	if (g_main_screen == NULL
			|| screen_find_client(g_main_screen,
					      g_dispatch_client->socket) == NULL) {
		/* TODO, optimize  this lookup is not needed
		 * accumulate sync grid for unfocused screens
		 * and/or use a message to coordinate focused status with clients
		 * if they don't need to render in background wastefully.
		 */
		return 0;
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
			cl = server_getclient(evfd);
			if (cl == NULL) {
				printf("client(%d) missing\n", evfd);
				server_remove_client(evfd);
				continue;
			}
			if (cl->handshaking) {
				server_remove_pending(cl->socket);
				if (spr16_server_handshake(cl)) {
					printf("handshake failed\n");
					server_free_client(cl);
					continue;
				}
				if (server_connect_client(cl)) {
					printf("connect failed\n");
					server_free_client(cl);
				}
				else {
					printf("handshake complete\n");
				}
				continue;
			}

			/* normal read and dispatch */
			msgbuf = spr16_read_msgs(evfd, &msglen);
			if (msgbuf == NULL) {
				printf("read_msgs: %s\n", STRERR);
				server_remove_client(evfd);
				continue;
			}
			g_dispatch_client = cl;
			if (spr16_dispatch_msgs(evfd, msgbuf, msglen)) {
				g_dispatch_client = NULL;
				printf("dispatch_msgs: %s\n", STRERR);
				server_remove_client(evfd);
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
	while (g_main_screen)
	{
		struct screen *next_screen = g_main_screen->next;
		while (g_main_screen->clients)
		{
			struct client *next_client = g_main_screen->clients->next;
			spr16_send_ack(g_main_screen->clients->socket,
					SPR16_NACK, SPRITENACK_DISCONNECT);
			server_free_client(g_main_screen->clients);
			g_main_screen->clients = next_client;
		}
		free(g_main_screen);
		g_main_screen = next_screen;
	}
	close(listen_fd);
	close(g_epoll_fd);
	g_epoll_fd = -1;
	g_main_screen = NULL;
	return 0;
}

static void server_sync_fullscreen()
{
	struct client *cl = g_main_screen->clients;
	while (cl)
	{
		struct spr16_msgdata_sync sync;
		sync.x = 0;
		sync.y = 0;
		sync.width = cl->sprite.width;
		sync.height = cl->sprite.height;
		g_dispatch_client = cl;
		spr16_server_sync(&sync);
		g_dispatch_client = NULL;
		cl = cl->next;
	}
}

/*
 * -1  next screen
 * -2  prev screen
 * >=0 TODO screenid
 *
 */
static int main_screen_switch(int scrn)
{
	struct screen *oldmain, *tmp;
	if (g_main_screen == NULL) {
		printf("no main\n");
		return 0;
	}

	/* next */
	if (scrn == -1) {
		if (g_main_screen->next == NULL) {
			return 0;
		}
		oldmain = tmp = g_main_screen;
		g_main_screen = oldmain->next;
		while (tmp->next)
		{
			tmp = tmp->next;
		}
		tmp->next = oldmain;
		oldmain->next = NULL;
	}
	else if (scrn == -2) { /* prev */

	}
	else if (scrn >= 0) { /* idx */

	}
	else {
		return -1;
	}
	server_sync_fullscreen();
	return 0;
}


int hotkey_callback(uint32_t hk, void *v)
{
	switch (hk)
	{
		case SPR16_HOTKEY_NEXTSCREEN:
		case SPR16_HOTKEY_PREVSCREEN:
			return main_screen_switch(-1);
		default:
			return -1;
	}
	(void)v;
}

int spr16_server_init_input()
{
	load_linux_input_drivers(&g_input_devices, g_epoll_fd, 0, 1, &hotkey_callback);
	return 0;
}

