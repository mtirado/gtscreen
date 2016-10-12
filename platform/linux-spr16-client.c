/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
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
#include <linux/fcntl.h>
#include <linux/memfd.h>
#include "../protocol/spr16.h"

#define STRERR strerror(errno)
#define MAX_EPOLL 10

/* TODO
 * If a client needs multiple sprites we need to have support
 * for multiple regions, right now it's just one big chunk of shmem.
 */
input_handler g_input_func;
struct spr16 g_sprite;
struct spr16_msgdata_servinfo g_servinfo;
struct epoll_event g_events[MAX_EPOLL];
struct epoll_event g_ev;
int g_epoll_fd;
int g_socket;
int g_handshaking;

struct spr16_msgdata_servinfo *spr16_client_get_servinfo()
{
	return &g_servinfo;
}
struct spr16 *spr16_client_get_sprite()
{
	return &g_sprite;
}

/*
 * name is the vt we are on. e.g. tty1
 * returns connected socket
 */
int spr16_client_connect(char *name)
{
	struct sockaddr_un addr;
	/* TODO move this stuff into an init function ?? */
	g_input_func = NULL;
	memset(&addr, 0, sizeof(addr));
	g_epoll_fd = -1;
	g_socket = -1;
	g_handshaking = 1;
	memset(&g_servinfo, 0, sizeof(g_servinfo));
	memset(&g_sprite, 0, sizeof(g_sprite));

	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", SPRITE_SOCKPATH, name);
	/* TODO check perms, sticky bit on dir, etc */
	g_socket = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
	if (g_socket == -1)
		return -1;

	if (connect(g_socket, (struct sockaddr *)&addr, sizeof(addr))) {
		printf("connect(%s): %s\n", addr.sun_path, STRERR);
		return -1;
	}

	g_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (g_epoll_fd == -1) {
		printf("epoll_create1: %s\n", STRERR);
		close(g_socket);
		return -1;
	}

	memset(&g_ev, 0, sizeof(g_ev));
	g_ev.events = EPOLLIN;
	g_ev.data.fd = g_socket;
	if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_socket, &g_ev)) {
		printf("epoll_ctl(add): %s\n", STRERR);
		goto failure;
	}
	printf("client connected\n");
	return g_socket;
failure:
	close(g_epoll_fd);
	close(g_socket);
	return -1;
}

/* TODO check to see if glibc decided to finally implement this, or what? */
#ifdef __i386__
/* including fcntl.h is giving me redefinitions, this is hacky and i'm sorry */
extern int fcntl(int __fd, int __cmd, ...);
int memfd_create(const char *__name, unsigned int __flags)
{
	int retval = -1;
	__asm__("movl $356, %eax");    /* syscall number */
	__asm__("movl 8(%ebp), %ebx"); /* name */
	__asm__("movl 12(%ebp),%ecx"); /* flags */
	__asm__("int $0x80");
	__asm__("movl %%eax, %0" : "=q" (retval));
	(void) __name;
	(void) __flags;
	return retval;
}
#else
	#error "unsupported kernel arch (no syscall wrapper for memfd, afaik)"
#endif

static int spr16_create_memfd(uint16_t width, uint16_t height, uint8_t bpp)
{
	uint32_t shmsize;
	int memfd = -1;
	char *mem = NULL;
	unsigned int seals;
	unsigned int checkseals;

	/* TODO round up for vectorized ops? */
	shmsize = width * height * bpp;
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
	mem = mmap(0, shmsize, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, 0);
	if (mem == MAP_FAILED) {
		printf("mmap error: %s\n", STRERR);
		goto failure;
	}

	/* seal size */
	seals =	  F_SEAL_SHRINK
		| F_SEAL_GROW
		| F_SEAL_SEAL;
		/*| F_SEAL_WRITE_PEER */
	if (fcntl(memfd, F_ADD_SEALS, seals) == -1) {
		printf("seal error: %s\n", STRERR);
		goto failure;
	}
	checkseals = (unsigned int)fcntl(memfd, F_GET_SEALS);
	if (checkseals != seals) {
		goto failure;
	}
	g_sprite.shmem.size = shmsize;
	g_sprite.shmem.mem  = mem;
	g_sprite.shmem.fd   = memfd;
	return 0;

failure:
	close(memfd);
	if (mem) {
		if (munmap(mem, shmsize)) {
			printf("munmap failed; %s\n", STRERR);
		}
	}
	return -1;
}

int spr16_client_handshake_start(char *name, uint16_t width, uint16_t height)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_register_sprite data;
	uint16_t bpp = 32;
	memset(&hdr, 0, sizeof(hdr));
	memset(&data, 0, sizeof(data));

	if (width > g_servinfo.maxwidth || height > g_servinfo.maxheight) {
		printf("sprite size(%d, %d) -- server max(%d, %d)\n",
				width,height,g_servinfo.maxwidth,g_servinfo.maxheight);
		return -1;
	}
	hdr.type = SPRITEMSG_REGISTER_SPRITE;
	data.width = width;
	data.height = height;
	data.bpp = bpp;
	snprintf(data.name, SPRITE_MAXNAME, "%s", name);
	if (spr16_write_msg(g_socket, &hdr, &data, sizeof(data))) {
		printf("spr16_servinfo write_msg: %s\n", STRERR);
		return -1;
	}
	g_sprite.bpp = bpp;
	g_sprite.width = width;
	g_sprite.height = height;
	printf("client send register\n");
	return 0;
}

int spr16_client_servinfo(struct spr16_msgdata_servinfo *sinfo)
{
	/* this info is static, msg is expected only once */
	if (g_servinfo.maxbpp)
		return -1;
	if (!sinfo->maxbpp || !sinfo->maxwidth || !sinfo->maxheight)
		return -1;
	printf("client: read msg\n");
	printf("max width: %d\n", sinfo->maxwidth);
	printf("max width: %d\n", sinfo->maxheight);
	printf("max bpp: %d\n",   sinfo->maxbpp);
	memcpy(&g_servinfo, sinfo, sizeof(g_servinfo));

	/* TODO uhhh fix this. add handler for servinfo i guess? */
	if (spr16_client_handshake_start("TEST", 800, 600))
		return -1;

	return 0;
}

/* ack / nack */
static int handle_ack(struct spr16_msgdata_ack *ack)
{
	switch (ack->info)
	{
	case SPRITEACK_SEND_DESCRIPTOR:
		if (spr16_create_memfd(g_sprite.width,
				       g_sprite.height,
				       g_sprite.bpp)) {
			printf("could not create memfd\n");
			return -1;
		}
		if (afunix_send_fd(g_socket, g_sprite.shmem.fd)) {
			printf("could not send descriptor\n");
			return -1;
		}
		break;
	case SPRITEACK_ESTABLISHED:
		g_handshaking = 0;
		break;
	default:
		return -1;
	}
	return 0;
}
static int handle_nack(struct spr16_msgdata_ack *nack)
{
	/* TODO */
	switch (nack->info)
	{
	default:
		return -1;
	}
}
int spr16_client_ack(struct spr16_msgdata_ack *ack)
{
	if (ack->ack)
		return handle_ack(ack);
	else
		return handle_nack(ack);
}

int spr16_client_sync(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_sync data;
	/*memset(&data, 0, sizeof(data));*/
	if (g_handshaking) {
		return 0;
	}

	hdr.type = SPRITEMSG_SYNC;
	data.x = x;
	data.y = y;
	data.width = width;
	data.height = height;
	if (spr16_write_msg(g_socket, &hdr, &data, sizeof(data))) {
		printf("spr16_servinfo write_msg: %s\n", STRERR);
		return -1;
	}
	return 0;
}

/* TODO
 * does client need hooks for received message notifications?
 * ^ ^ yes ^ ^
 */
int spr16_client_update()
{
	int i;
	int evcount;
	char *msgbuf;
	uint32_t msglen;
interrupted:
	evcount = epoll_wait(g_epoll_fd, g_events, MAX_EPOLL, 0);
	if (evcount == -1) {
		if (errno == EINTR) {
			goto interrupted;
		}
		printf("epoll_wait: %s\n", STRERR);
		return -1;
	}
	for (i = 0; i < evcount; ++i)
	{
		if (g_events[i].data.fd == g_socket) {
			/* read and dispatch */
			msgbuf = spr16_read_msgs(g_socket, &msglen);
			if (msgbuf == NULL) {
				printf("read_msgs: %s\n", STRERR);
				return -1;
			}
			if (spr16_dispatch_msgs(g_socket, msgbuf, msglen)) {
				printf("dispatch_msgs: %s\n", STRERR);
				return -1;
			}
		}
		else {
			errno = EBADF;
			return -1;
		}
	}
	return 0;
}

int spr16_client_handshake_wait(uint32_t timeout)
{
	if (timeout < 100)
		timeout = 100;
	while(timeout)
	{
		/* milliseconds */
		usleep(1000);
		if (spr16_client_update())
			return -1;
		if (!g_handshaking)
			break;
		--timeout;
	}
	return 0;
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

/* TODO, either unify all input messages, or split up into multiple handlers */
int spr16_client_set_input_handler(input_handler func)
{
	g_input_func = func;
	return 0;
}
int spr16_client_input_keyboard(struct spr16_msgdata_input_keyboard *ki)
{
	if (!g_input_func)
		return 0;
	return g_input_func(ki->flags, ki->keycode);
}





