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
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <time.h>

#include <fcntl.h>
#define _ASM_GENERIC_FCNTL_H /* avoid redefinition.. WTF!? */
#define F_LINUX_SPECIFIC_BASE 1024 /* this better not change per-arch... */
#include <linux/fcntl.h>
#include <linux/memfd.h>

#include "../../spr16.h"
#include "../../screen.h"
#include "../fdpoll-handler.h"
#include "platform.h"
#include "vt.h"
#include "fb.h"

sig_atomic_t g_input_muted; /* don't forward input if muted */
sig_atomic_t g_unmute_input;
sig_atomic_t g_is_active;
extern struct drm_kms *g_card0; /* last remaining drm specific hack */

#define MAX_ACCEPT 5
#define STRERR strerror(errno)

int client_callback(int fd, int event_flags, void *user_data);
int listener_callback(int fd, int event_flags, void *user_data);
int hotkey_callback(uint32_t hk, void *v);


static struct client *server_remove_pending(struct server_context *self, int fd)
{
	struct client **trail = &self->pending_clients;
	struct client     *cl =  self->pending_clients;
	while (cl)
	{
		if (cl->socket == fd) {
			*trail = cl->next;
			cl->next = NULL;
			break;
		}
		trail = &cl->next;
		cl = cl->next;
	}
	return cl;
}

static struct client *server_getclient(struct server_context *self, int fd)
{
	struct client *cl = NULL;
	struct screen *scrn;
	scrn = self->main_screen;
	while (scrn)
	{
		cl = screen_find_client(scrn, fd);
		if (cl)
			return cl;
		scrn = scrn->next;
	}

	cl = self->pending_clients;
	while (cl)
	{
		if (cl->socket == fd) {
			return cl;
		}
		cl = cl->next;
	}
	return NULL;
}

static int in_free_list(struct server_context *self, struct client *cl)
{
	unsigned int i;
	for (i = 0; i < self->free_count; ++i)
	{
		if (self->free_list[i] == cl)
			return 1;
	}
	return 0;
}

static int server_free_client(struct server_context *self, struct client *cl)
{
	if (self->free_count < MAX_FDPOLL_HANDLER) {
		if (!in_free_list(self, cl)) {
			if (fdpoll_handler_remove(self->fdpoll, cl->socket)) {
				printf("couldn't remove handler for client %d\n", cl->socket);
				return -1;
			}
			self->free_list[self->free_count] = cl;
			++self->free_count;
			return 0;
		}
	}
	printf("free client not found %d\n", cl->socket);
	return -1;
}

/* TODO pass client instead of fd */
static int server_remove_client(struct server_context *self, int fd)
{
	struct screen *scrn;
	struct screen **trail;
	struct client *cl;
	errno = 0;
	scrn  =  self->main_screen;
	trail = &self->main_screen;
	while (scrn)
	{
		struct client *focused_client = scrn->clients;
		if (focused_client == self->main_screen->clients) {
			input_flush_all_devices(self->input_devices);
		}
		cl = screen_remove_client(scrn, fd);
		if (cl) {
			spr16_send_nack(fd, SPRITENACK_DISCONNECT);
			if (server_free_client(self, cl))
				return -1;
			*trail = scrn->next;
			/* TODO, clear region? */
			free(scrn); /* TODO, dont free, for multi-client... */
			server_sync_fullscreen(self);
			return 0;
		}
		trail = &scrn->next;
		scrn  =  scrn->next;
	}
	cl = server_remove_pending(self, fd);
	if (cl == NULL) {
		printf("ERROR: server_remove_client unknown fd(%d)\n", fd);
		errno = ESRCH;
		return -1;
	}

	server_free_client(self, cl);
	return 0;
}

static int server_connect_client(struct server_context *self, struct client *cl)
{
	struct screen *scrn, *tmp;

	scrn = calloc(1, sizeof(struct screen));
	if (scrn == NULL)
		return -1;
	if (screen_init(scrn))
		goto err;
	if (!server_remove_pending(self, cl->socket)) {
		printf("pending client not found\n");
		goto err;
	}
	if (screen_add_client(scrn, cl))
		goto err;
	/* add to end of screen list */
	tmp = self->main_screen;
	if (tmp) {
		while (tmp->next)
		{
			tmp = tmp->next;
		}
		tmp->next = scrn;
		printf("-- new screen added to existing list --\n");
	}
	else {
		self->main_screen = scrn;
		printf("-- new screen added to empty list --\n");
	}
	cl->connected = 1;
	cl->handshaking = 0;
	return 0;
err:
	free(scrn);
	return -1;
}

static int spr16_server_servinfo(struct server_context *self, int fd)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_servinfo data;
	memset(&hdr, 0, sizeof(hdr));
	memset(&data, 0, sizeof(data));

	hdr.type    = SPRITEMSG_SERVINFO;
	data.width  = self->fb->width;
	data.height = self->fb->height;
	data.bpp    = self->fb->bpp;
again:
	if (spr16_write_msg(fd, &hdr, &data, sizeof(data))) {
		if (errno == EAGAIN)
			goto again;
		return -1;
	}
	return 0;
}

static struct cl_cb_data *cb_data_add(struct server_context *self, struct client *cl)
{
	struct cl_cb_data *data = calloc(1, sizeof(struct cl_cb_data));
	if (data == NULL)
		return NULL;
	data->next = self->cb_data;
	data->self = self;
	data->cl = cl;
	self->cb_data = data;
	return data;
}

static int cb_data_remove(struct server_context *self, struct client *cl)
{
	struct cl_cb_data **trail, *cb_data;
	if (cl == NULL)
		return -1;
	trail = &self->cb_data;
	cb_data = self->cb_data;
	while (cb_data)
	{
		if (cb_data->cl == cl) {
			*trail = cb_data->next;
			free(cb_data);
			return 0;
		}
		trail = &cb_data->next;
		cb_data = cb_data->next;
	}
	return -1;
}

static int server_addclient(struct server_context *self, int fd)
{
	struct client *cl;
	struct cl_cb_data *cb_data;
	errno = 0;

	if (server_getclient(self, fd)) {
		errno = EEXIST;
		return -1;
	}

	cl = calloc(1, sizeof(struct client));
	if (cl == NULL) {
		return -1;
	}
	cl->sprite.shmem.fd = -1;

	/* send server info to client */
	if (spr16_server_servinfo(self, fd)) {
		goto err;
	}

	cl->connected = 0;
	cl->socket = fd;
	cl->next = self->pending_clients;
	self->pending_clients = cl;
	cb_data = cb_data_add(self, cl);
	if (cb_data == NULL)
		goto err;

	if (fdpoll_handler_add(self->fdpoll, fd, FDPOLLIN, client_callback, cb_data)) {
		printf("fdpoll_handler_add(%d) failed\n", fd);
		cb_data_remove(self, cl);
		goto err;
	}
	/* TODO, get creds and log uid/gid/pid */
	printf("client(%d)added to server\n", fd);
	return 0;
err:
	close(cl->socket);
	free(cl);
	return -1;
}

static int client_handshake(struct server_context *self, struct client *cl)
{
	if (!cl->handshaking)
		return -1;
	if (server_connect_client(self, cl)) {
		printf("handshake connect failed\n");
		return -1;
	}
	return 0;
}

static int handle_ack(struct server_context *self, struct client *cl, struct spr16_msgdata_ack *ack)
{
	switch (ack->info)
	{
		case SPRITEACK_ESTABLISHED:
			if (client_handshake(self, cl))
				return -1;
			break;
		case SPRITEACK_SEND_FD:
			if (!cl->recv_fd_wait)
				return -1;
			if (cl->sprite.shmem.fd <= 0)
				return -1;
			if (afunix_send_fd(cl->socket, cl->sprite.shmem.fd)) {
				printf("could not send descriptor\n");
				return -1;
			}
			cl->recv_fd_wait = 0;
			break;

		default:
			printf("unknown ack info: %d\n", ack->info);
			return -1;
	}
	return 0;
}

static int handle_nack(struct server_context *self, struct client *cl, struct spr16_msgdata_ack *ack)
{
	(void)self;
	(void)cl;
	switch (ack->info)
	{
		case SPRITENACK_SHMEM:
			printf("nack, shmem\n");
			break;
		case SPRITENACK_DISCONNECT:
			printf("client disconnected\n");
			break;
		case SPRITENACK_FD:
			printf("send_fd failed\n");
			break;
		default:
			printf("unknown nack info: %d\n", ack->info);
			break;
	}
	return -1;
}

/* including fcntl.h is giving me redefinitions, this is hacky and i'm sorry */
extern int fcntl(int __fd, int __cmd, ...);
int memfd_create(const char *__name, unsigned int __flags)
{
	return syscall(SYS_memfd_create, __name, __flags);
}

int spr16_create_memfd(struct client *cl)
{
	uint32_t shmsize;
	int memfd = -1;
	char *addr = NULL;
	unsigned int seals;
	unsigned int checkseals;
	uint16_t width = cl->sprite.width;
	uint16_t height = cl->sprite.height;
	uint8_t bpp = cl->sprite.bpp;

	shmsize = width * height * (bpp/8);
	if (!shmsize)
		return -1;

	/* create sprite memory region */
	memfd = memfd_create("sprite16", MFD_ALLOW_SEALING);
	if (memfd == -1) {
		printf("create error: %s\n", STRERR);
		return -1;
	}
       	if (ftruncate(memfd, shmsize) == -1) {
		printf("truncate error: %s\n", STRERR);
		goto failure;
	}

	addr = mmap(0, shmsize, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, 0);
	if (addr == MAP_FAILED) {
		printf("mmap(%d) error: %s\n", memfd, STRERR);
		goto failure;
	}

	/* seal size */
	seals =	  F_SEAL_SHRINK
		| F_SEAL_GROW
		| F_SEAL_SEAL;
	if (fcntl(memfd, F_ADD_SEALS, seals) == -1) {
		printf("seal error: %s\n", STRERR);
		goto failure;
	}
	checkseals = (unsigned int)fcntl(memfd, F_GET_SEALS);
	if (checkseals != seals) {
		goto failure;
	}
	cl->sprite.shmem.size = shmsize;
	cl->sprite.shmem.addr = addr;
	cl->sprite.shmem.fd   = memfd;
	return memfd;

failure:
	close(memfd);
	if (addr) {
		if (munmap(addr, shmsize)) {
			printf("munmap failed; %s\n", STRERR);
		}
	}
	return -1;
}

static int spr16_server_register_sprite(struct server_context *self, int fd, struct spr16_msgdata_register_sprite *reg)
{
	/* TODO, pass cl directly  */
	struct client *cl = server_getclient(self, fd);

	if (cl == NULL)
		return -1;

	/* register happens only once */
	if (cl->handshaking || cl->connected) {
		printf("bad client\n");
		return -1;
	}
	if (reg->width > self->fb->width || !reg->width) {
		printf("bad width\n");
		spr16_send_nack(fd, SPRITENACK_WIDTH);
		return -1;
	}
	if (reg->height > self->fb->height || !reg->height) {
		printf("bad height\n");
		spr16_send_nack(fd, SPRITENACK_HEIGHT);
		return -1;
	}
	if (reg->bpp > self->fb->bpp || reg->bpp < 8) {
		printf("bad bpp\n");
		spr16_send_nack(fd, SPRITENACK_BPP);
		return -1;
	}
	cl->handshaking = 1;
	cl->sprite.bpp = reg->bpp;
	cl->sprite.width = reg->width;
	cl->sprite.height = reg->height;
	cl->sprite.shmem.size = (reg->bpp/8) * reg->width * reg->height;
	cl->sprite.shmem.addr = NULL;
	cl->sprite.flags = reg->flags;

	printf("client requesting sprite(%dx%d:%d)\n", reg->width, reg->height, reg->bpp);

	if (cl->sprite.flags & SPRITE_FLAG_DIRECT_SHM) {
		char *addr = NULL;
		int prime_fd;
		uint16_t width = cl->sprite.width;
		uint16_t height = cl->sprite.height;
		uint8_t bpp = cl->sprite.bpp;
		size_t size = width * height * (bpp/8);
		/* TODO make all drm calls from main.c, get rid of struct in server
		 * and only allow this if client requests full screen, unless overlays
		 */
		if (drm_prime_export_fd(g_card0->card_fd, g_card0->sfb, &prime_fd)) {
			printf("could not export prime fd\n");
			spr16_send_nack(fd, SPRITENACK_SHMEM);
			return -1;
		}

		/* FIXME we don't actually need this mapped but for the time being,
		 * there may be some code that assumes shmem.addr is valid */
		printf("calling prime mmap.....\n");
		addr = mmap(0, size, PROT_WRITE|PROT_READ, MAP_SHARED, prime_fd, 0);
		if (addr == MAP_FAILED || addr == NULL) {
			printf("mmap error: %s\n", STRERR);
			return -1;
		}

		cl->sprite.shmem.addr = addr;
		cl->sprite.shmem.fd = prime_fd;
		cl->sprite.shmem.size = g_card0->sfb->size;
	}
	else {
		if (spr16_create_memfd(cl) == -1) {
			printf("could not create memfd\n");
			spr16_send_nack(fd, SPRITENACK_SHMEM);
			return -1;
		}
	}

	if (spr16_send_ack(fd, SPRITEACK_RECV_FD)) {
		printf("send_descriptor ack failed\n");
		return -1;
	}
	cl->recv_fd_wait = 1; /* don't send anything else until client is ready for fd */
	return 0;
}

static int open_log(char *socketname)
{
	char path[MAX_SYSTEMPATH];
	int fd;

	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	snprintf(path, sizeof(path), "%s/%s.log", SPR16_SOCKPATH, socketname);
	fd = open(path, O_WRONLY|O_CLOEXEC|O_CREAT|O_TRUNC, 0750);
	if (fd == -1) {
		printf("open log (%s): %s\n", path, STRERR);
		return -1;
	}
	fchmod(fd, 0750);

	if (dup2(fd, STDOUT_FILENO) != STDOUT_FILENO
			|| dup2(fd, STDERR_FILENO) != STDERR_FILENO) {
		printf("dup2: %s\n", STRERR);
		return -1;
	}
	return 0;
}

static int server_create_socket(struct server_context *self, char *sockname)
{
	int sock;
	struct sockaddr_un addr;
	uid_t euid;

	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_UNIX;
	if (snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s",
				SPR16_SOCKPATH, sockname) >= (int)sizeof(addr.sun_path))
		return -1;

	sock = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
	if (sock == -1) {
		printf("socket: %s\n", STRERR);
		return -1;
	}

	/* create socket as real uid if we are running with setuid bit */
	euid = geteuid();
	if (seteuid(getuid()))
		return -1;
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr))) {
		printf("    >>  socket already exists: %s  <<    \n", addr.sun_path);
		printf("bind: %s\n", STRERR);
		close(sock);
		return -1;
	}
	if (seteuid(euid))
		return -1;

	/* TODO no authentication for connecting to socket, could add a whitelist to
	 * server and check peers uid/gid in ancillary data, or use some token */
	chmod(addr.sun_path, 0777);

	if (listen(sock, MAX_ACCEPT)) {
		printf("listen: %s\n", STRERR);
		close(sock);
		return -1;
	}

	if (fdpoll_handler_add(self->fdpoll, sock, FDPOLLIN, listener_callback, self)) {
		printf("fdpoll_handler_add(%d) failed\n", sock);
		close(sock);
		return -1;
	}

	if (open_log(sockname))
		return -1;

	return sock;
}

struct server_context *spr16_server_init(char *sockname,
					 struct fdpoll_handler *fdpoll,
					 struct spr16_framebuffer *fb)
{
	struct server_context *self = calloc(1, sizeof(struct server_context));
	if (self == NULL)
		return NULL;
	g_is_active = 1;
	self->main_screen = NULL;
	self->input_devices = NULL;
	self->pending_clients = NULL;
	self->free_count = 0;
	self->fdpoll = fdpoll;
	self->listen_fd = server_create_socket(self, sockname);
	self->card0 = g_card0;
	if (self->listen_fd == -1) {
		free(self);
		return NULL;
	}
	self->fb = fb;

	/* TODO maybe turn off kbd if using evdev, but i like having the kernel
	 * trigger vt switching, despite the xorg alt-keystate annoyances */
	load_linux_input_drivers(self, 0, 1, &hotkey_callback);

	return self;

/*free_ret:
	close(self->listen_fd);
	free(self);
	return NULL;*/
}

void spr16_server_activate()
{
	g_is_active = 1;
}

void spr16_server_deactivate()
{
	g_is_active = 0;
}

int spr16_server_is_active()
{
	return g_is_active;
}

/* i really want TODO some smarter damage merging. at very least a grid that
 * prevents syncing the same region twice would be the best optimization,
 * when e.g. rapidly syncing large regions in async mode...
 */
static void accumulate_dmg(struct client *cl, struct spr16_msgdata_sync dmg)
{
	const uint16_t last_idx = SPR16_DMG_SLOTS - 1;

	if (cl->dmg_count < SPR16_DMG_SLOTS) {
		cl->dmg[cl->dmg_count] = dmg;
		++cl->dmg_count;
	}
	else {
		struct spr16_msgdata_sync old = cl->dmg[last_idx];
		if (dmg.xmin < old.xmin)
			cl->dmg[last_idx].xmin = dmg.xmin;
		if (dmg.ymin < old.ymin)
			cl->dmg[last_idx].ymin = dmg.ymin;
		if (dmg.xmax > old.xmax)
			cl->dmg[last_idx].xmax = dmg.xmax;
		if (dmg.ymax > old.ymax)
			cl->dmg[last_idx].ymax = dmg.ymax;
	}
}

static int add_sync_client(struct server_context *self, struct client *cl)
{
	int i;
	if (cl->syncing)
		return 0;
	for (i = 0; i < SPR16_MAXCLIENTS; ++i)
	{
		if (self->sync_clients[i] == NULL)
		{
			self->sync_clients[i] = cl;
			cl->syncing = 1;
			return 0;
		}
	}
	return -1;
}

static int spr16_server_sync(struct server_context *self,
		       struct client *cl,
		       uint16_t flags,
		       struct spr16_msgdata_sync *region)
{
	struct spr16_msgdata_sync dmg;

	if (region->xmax >= self->fb->width
			|| region->ymax >= self->fb->height
			|| region->xmin >= self->fb->width
			|| region->ymin >= self->fb->height
			|| region->xmax < region->xmin
			|| region->ymax < region->ymin) {
		printf("bad sync parameters(%d, %d, %d, %d)\n",
				region->xmin, region->ymin, region->xmax, region->ymax);
		return -1;
	}


	if (!self->fb->addr || !cl->sprite.shmem.addr) {
		printf("bad ptr %p %p\n", (void *)self->fb->addr,
					  (void *)cl->sprite.shmem.addr);
		return -1;
	}

	dmg.xmin   = region->xmin;
	dmg.xmax   = region->xmax;
	dmg.ymin   = region->ymin;
	dmg.ymax   = region->ymax;
	accumulate_dmg(cl, dmg);

	if (flags & ~(SPRITESYNC_FLAG_MASK)) {
		return -1;
	}
	else if (flags == 0)
		return 0;

	cl->sync_flags |= flags;
	return add_sync_client(self, cl);
}

static int server_free_list(struct server_context *self)
{
	int ret = 0;
	if (self->free_count) {
		struct client *cl;
		unsigned int i;
		for (i = 0; i < self->free_count; ++i)
		{
			cl = self->free_list[i];

			close(cl->socket);
			/* free shared memory */
			if (cl->sprite.shmem.size) {
				/*memset(cl->sprite.shmem.addr,0xAA,cl->sprite.shmem.size);*/
				close(cl->sprite.shmem.fd);
				if (munmap(cl->sprite.shmem.addr,cl->sprite.shmem.size)){
					printf("ERROR: munmap shm: %p, %d: %s\n",
						(void *)cl->sprite.shmem.addr,
						cl->sprite.shmem.size, STRERR);
					ret = -1;
				}
			}
			free(cl);
			self->free_list[i] = NULL;
		}
		self->free_count = 0;
	}
	return ret;
}

int spr16_server_update(struct server_context *self)
{
	/* this is where the server blocks.
	 * -1 indefinite, 0 immediate, >0 milliseconds */
	if (fdpoll_handler_poll(self->fdpoll, -1)) {
		printf("fdpoll_handler_poll failed\n");
		return -1;
	}

	if (self->sync_clients[0]) {
		int i;
		for (i = 0; i < SPR16_MAXCLIENTS; ++i)
		{
			struct client *cl = self->sync_clients[i];
			if (cl == NULL)
				break;
			if (fb_sync_client(self, cl))
				printf("fb_sync_client(%d) failed\n", cl->socket);
			cl->syncing = 0;
			self->sync_clients[i] = NULL;
		}
	}

	if (server_free_list(self)) {
		printf("free_list() failed\n");
		return -1;
	}

	return 0;
}

int spr16_server_shutdown(struct server_context *self)
{
	printf("--- server shutdown ---\n");
	while (self->main_screen)
	{
		struct screen *next_screen = self->main_screen->next;
		while (self->main_screen->clients)
		{
			struct client *next_client = self->main_screen->clients->next;
			spr16_send_nack(self->main_screen->clients->socket,
					SPRITENACK_DISCONNECT);
			server_free_client(self, self->main_screen->clients);
			self->main_screen->clients = next_client;
		}
		free(self->main_screen);
		self->main_screen = next_screen;
	}
	self->main_screen = NULL;
	server_free_list(self);
	close(self->listen_fd);
	free(self);
	return 0;
}

void server_sync_fullscreen(struct server_context *self)
{
	struct client *cl;

	if (self->main_screen == NULL)
		return;

	cl = self->main_screen->clients;
	while (cl)
	{
		/* will have to choose between flip/vblank when flip is implemented*/
		uint16_t flags = SPRITESYNC_FLAG_ASYNC|SPRITESYNC_FLAG_VBLANK;
		struct spr16_msgdata_sync sync;
		sync.xmin = 0;
		sync.ymin = 0;
		sync.xmax = cl->sprite.width-1;
		sync.ymax = cl->sprite.height-1;
		spr16_server_sync(self, cl, flags, &sync);
		cl = cl->next;
	}
}

int spr16_server_reset_client(struct client *cl)
{
	struct spr16_msgdata_input data;
	struct spr16_msghdr hdr;

	/* any clients trying to track keystates should reset */
	hdr.type = SPRITEMSG_INPUT;
	data.type = SPR16_INPUT_CONTROL;
	data.code = SPR16_CTRLCODE_RESET;

	if (spr16_write_msg(cl->socket, &hdr, &data, sizeof(data))) {
		return -1;
	}
	return 0;
}

static int focus_client(struct client *cl)
{
	if (!cl)
		return -1;
	return spr16_server_reset_client(cl);
}

/*
 * -1  next screen
 * -2  prev screen
 * >=0 TODO screenid
 */
static int main_screen_switch(struct server_context *self, int scrn)
{
	if (self->main_screen == NULL) {
		return 0;
	}

	/* next */
	if (scrn == -1) {
		struct screen *oldmain, *tmp;
		if (self->main_screen->next == NULL) {
			printf("main_screen next was null\n");
			return 0;
		}
		oldmain = tmp = self->main_screen;
		self->main_screen = oldmain->next;
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
	input_flush_all_devices(self->input_devices);
	server_sync_fullscreen(self);
	focus_client(self->main_screen->clients);
	return 0;
}

/* TODO hotkey mapping */
int hotkey_callback(uint32_t hk, void *v)
{
	struct server_context *self = v;
	switch (hk)
	{
		case SPR16_HOTKEY_AXE:
			raise(SIGTERM);
			break;
		case SPR16_HOTKEY_NEXTSCREEN:
		case SPR16_HOTKEY_PREVSCREEN:
			return main_screen_switch(self, -1);
		default:
			return -1;
	}
	return 0;
}

int listener_callback(int fd, int event_flags, void *user_data)
{
	struct server_context *self = user_data;
	printf("listener callback fd=%d\n", fd);
	if (event_flags & FDPOLLHUP) {
		printf("listener HUP(%d)\n", fd);
		return FDPOLL_HANDLER_REMOVE;
	}
	else if (event_flags & FDPOLLERR) {
		printf("listener ERR\n");
		return FDPOLL_HANDLER_REMOVE;
	}
	else if (event_flags & EPOLLIN) {
		int newsock;
		struct sockaddr_un addr;
		socklen_t addrlen = sizeof(addr);
		memset(&addr, 0, sizeof(addr));
		newsock = accept4(fd, (struct sockaddr *)&addr,
				&addrlen, SOCK_NONBLOCK|SOCK_CLOEXEC);
		if (newsock == -1) {
			printf("accept4: %s\n", STRERR);
			return FDPOLL_HANDLER_OK;
		}
		printf("about to call addclient...\n");
		/* TODO we should rate limit this per uid, and timeout drop,
		 * also add a handshake timeout */
		if (server_addclient(self, newsock)) {
			printf("server_addclient(%d) failed\n", newsock);
			if (errno == EEXIST)
				printf("client already exists\n");
			close(newsock);
			return FDPOLL_HANDLER_OK;
		}
	}

	return FDPOLL_HANDLER_OK;
}

int spr16_server_ack(struct server_context *self, struct client *cl, struct spr16_msgdata_ack *ack)
{
	if (ack->ack)
		return handle_ack(self, cl, ack);
	else
		return handle_nack(self, cl, ack);
}
int spr16_dispatch_server_msgs(struct server_context *self, struct client *cl, char *msgbuf, uint32_t buflen)
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
			if (spr16_server_servinfo(self, cl->socket)) {
				printf("servinfo failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_REGISTER_SPRITE:
			if (spr16_server_register_sprite(self, cl->socket,
					(struct spr16_msgdata_register_sprite *)
					msgdata)) {
				printf("register failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_SYNC:
			if (spr16_server_sync(self, cl, msghdr->bits,
						(struct spr16_msgdata_sync *)msgdata)){
				printf("sync failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_ACK:
			if (spr16_server_ack(self, cl,
					     (struct spr16_msgdata_ack *)msgdata)) {
				return -1;
			}
			break;
		default:
			printf("unknown msg type\n");
			errno = EPROTO;
			return -1;
		}
		rdpos += sizeof(struct spr16_msghdr) + typelen;
	}

	return 0;
}

int client_callback(int fd, int event_flags, void *user_data)
{
	char *msgbuf;
	uint32_t msglen;
	struct cl_cb_data *dat;
	struct server_context *self;
	struct client *cl;


	dat = user_data;
	self = dat->self;
	cl = dat->cl;
	if (cl == NULL) {
		printf("client(%d) missing\n", fd);
		if (server_remove_client(self, fd))
			goto remove_failed;
		cb_data_remove(self, cl);
		return FDPOLL_HANDLER_OK;
	}
	if (event_flags & (FDPOLLHUP|FDPOLLERR)) {
		if (server_remove_client(self, fd))
			goto remove_failed;
		cb_data_remove(self, cl);
		return FDPOLL_HANDLER_OK;
	}

	/* normal read and dispatch */
	msgbuf = spr16_read_msgs(fd, &msglen);
	if (msgbuf == NULL) {
		printf("read_msgs: %s\n", STRERR);
		if (server_remove_client(self, fd))
			goto remove_failed;
		cb_data_remove(self, cl);
		return FDPOLL_HANDLER_OK;
	}
	if (spr16_dispatch_server_msgs(self, cl,  msgbuf, msglen)) {
		printf("dispatch_server_msgs: %s\n", STRERR);
		if (server_remove_client(self, fd))
			goto remove_failed;
		cb_data_remove(self, cl);
		return FDPOLL_HANDLER_OK;
	}
	return FDPOLL_HANDLER_OK;

remove_failed:
	printf("failed removing client(%d)\n", fd);
	cb_data_remove(self, cl);
	return FDPOLL_HANDLER_REMOVE;
}
