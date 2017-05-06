/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * a separate input driver may not be needed, i need to do a little more
 * poking around to be certain. this works decently, though it could be
 * optimized a bit... anyway, look in to if we can run this with a single
 * driver, and if so remove that CLONE_VM hack needed to set the
 * faux11input driver fd, because it loads after graphics driver.
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
input_surface_handler g_input_surface_func;
servinfo_handler g_servinfo_func;

/* TODO we probably want a client context */
static struct spr16 g_sprite;
static struct spr16_msgdata_servinfo g_servinfo;
static int g_socket;

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
	memset(&g_servinfo, 0, sizeof(g_servinfo));
	memset(&g_sprite, 0, sizeof(g_sprite));

	return 0;
}

/*
 * name is the vt we are on. e.g. tty1
 * returns connected socket
 */
int spr16_client_connect(char *name)
{
	struct sockaddr_un addr;

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

#ifdef __i386__
/* including fcntl.h is giving me redefinitions, this is hacky and i'm sorry */
extern int fcntl(int __fd, int __cmd, ...);
int memfd_create(const char *__name, unsigned int __flags)
{
	/* wow this is blows up on 4.8.3 when called from an xorg driver */
#if 0
	int retval = -1;
	__asm__("movl $356, %eax");    /* syscall number */
	__asm__("movl 8(%ebp), %ebx"); /* name */
	__asm__("movl 12(%ebp),%ecx"); /* flags */
	__asm__("int $0x80");
	__asm__("movl %%eax, %0" : "=q" (retval));
	(void) __name;
	(void) __flags;
	return retval;
#endif
	/* TODO detect arch, 356 is x86 */
	return syscall(356, __name, __flags);
}
#else
	#error "unsupported kernel arch (no syscall wrapper for memfd, afaik)"
#endif

int spr16_create_memfd(uint16_t width, uint16_t height, uint8_t bpp)
{
	uint32_t shmsize;
	int memfd = -1;
	char *addr = NULL;
	unsigned int seals;
	unsigned int checkseals;

	/* TODO round up for vectorized ops? */
	shmsize = width * height * (bpp/8);
	if (!shmsize)
		return -1;

	/* create sprite memory region */
	memfd = memfd_create("sprite16", MFD_ALLOW_SEALING);
	if (memfd == -1) {
		fprintf(stderr, "create error: %s\n", STRERR);
		return -1;
	}
       	if (ftruncate(memfd, shmsize) == -1) {
		fprintf(stderr, "truncate error: %s\n", STRERR);
		goto failure;
	}
	addr = mmap(0, shmsize, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "mmap error: %s\n", STRERR);
		goto failure;
	}

	/* seal size */
	seals =	  F_SEAL_SHRINK
		| F_SEAL_GROW
		| F_SEAL_SEAL;
		/*| F_SEAL_WRITE_PEER */
	if (fcntl(memfd, F_ADD_SEALS, seals) == -1) {
		fprintf(stderr, "seal error: %s\n", STRERR);
		goto failure;
	}
	checkseals = (unsigned int)fcntl(memfd, F_GET_SEALS);
	if (checkseals != seals) {
		goto failure;
	}
	g_sprite.shmem.size = shmsize;
	g_sprite.shmem.addr = addr;
	g_sprite.shmem.fd   = memfd;
	return 0;

failure:
	fprintf(stderr, "fail\n");
	close(memfd);
	if (addr) {
		if (munmap(addr, shmsize)) {
			fprintf(stderr, "munmap failed; %s\n", STRERR);
		}
	}
	return -1;
}

int spr16_client_handshake_start(char *name, uint16_t width, uint16_t height, uint32_t flags)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_register_sprite data;
	uint16_t bpp = 32;
	memset(&hdr, 0, sizeof(hdr));
	memset(&data, 0, sizeof(data));

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

/* ack / nack */
static int handle_ack(struct spr16_msgdata_ack *ack)
{
	switch (ack->info)
	{
	case SPRITEACK_SEND_DESCRIPTOR:
		if (spr16_create_memfd(g_sprite.width,
				       g_sprite.height,
				       g_sprite.bpp)) {
			fprintf(stderr, "could not create memfd\n");
			return -1;
		}
		if (afunix_send_fd(g_socket, g_sprite.shmem.fd)) {
			fprintf(stderr, "could not send descriptor\n");
			return -1;
		}
		break;
	case SPRITEACK_ESTABLISHED:
		g_handshaking = 0;
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
		fprintf(stderr, "nack: shared memory erro\n");
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

int spr16_client_sync(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_sync data;
	if (g_handshaking) {
		return 0;
	}

	hdr.type = SPRITEMSG_SYNC;
	data.x = x;
	data.y = y;
	data.width = width;
	data.height = height;
	if (spr16_write_msg(g_socket, &hdr, &data, sizeof(data))) {
		return -1;
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
		if (g_events[i].data.fd == g_socket) {
			msgbuf = spr16_read_msgs(g_socket, &msglen);
			if (msgbuf == NULL) {
				fprintf(stderr, "read_msgs: %s\n", STRERR);
				return -1;
			}
			if (spr16_dispatch_msgs(g_socket, msgbuf, msglen)) {
				fprintf(stderr, "dispatch_msgs: %s\n", STRERR);
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

int spr16_client_handshake_wait(uint32_t timeout)
{
	uint32_t c = 0;
	/* milliseconds */
	if (timeout < 100)
		timeout = 100;
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



