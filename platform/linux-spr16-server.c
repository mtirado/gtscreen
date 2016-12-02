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
#include <linux/input.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "../protocol/spr16.h"

/* VT switching */
#include <sys/ioctl.h>
#include <linux/vt.h>

/* TODO remove stupid drm hack here for sync msg */
#include "linux-drm.h"
extern struct drm_state g_state;


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
int g_keyboard_fd;
int g_soft_cursor; /* TODO */



/* input driver */
struct input_device;
typedef int (*input_read)(struct input_device *self, int client);
struct input_device {
	char name[32];
	struct input_device *next;
	input_read func_read;
	uint32_t private;
	int fd;
};
struct input_device *g_input_devices;
static int ascii_kbd_read(struct input_device *self, int client);
static int evdev_read(struct input_device *self, int client);


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
	if (spr16_write_msg(fd, &hdr, &data, sizeof(data))) {
		printf("spr16_servinfo write_msg: %s\n", STRERR);
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
	g_keyboard_fd = -1;
	g_width = width;
	g_height = height;
	g_bpp = bpp;
	return server_create_socket();
}

/* arggh USB keyboards just return EINVAL here, gggrrrrrreat!! */
#if 0
static int evdev_print_keycodes(struct input_device *dev)
{
	unsigned int i;

	printf("------------------------ KEY CODES ----------------------\n");
	printf(" scancode                                  keycode\n");
	for (i = 0; i < KEY_CNT; ++i)
	{
		struct input_keymap_entry km;
		memset(&km, 0, sizeof(km));
		km.len = 1;
		km.scancode[0] = i;
		km.keycode = i;
		/* theres a _V2 for 256bit scancode, why though? thats madness */
		if (ioctl(dev->fd, EVIOCGKEYCODE_V2, &km, 0)) {
			printf("EVIOCGKEYCODE: %s\n", STRERR);
			continue;
		}
			printf("%d                                     (%c)%d\n",
							i, km.keycode, km.keycode);

	}
	printf("---------------------------------------------------------\n");

	return 0;
}
#endif
/* add input descriptor(s) to epoll */
int spr16_server_init_input(int ascii_kbd, int evdev_kbd)
{
	struct epoll_event ev;

	if (ascii_kbd != -1) {
		int fl;
		struct input_device *dev;

		memset(&ev, 0, sizeof(ev));
		/* set to nonblocking */
		fl = fcntl(ascii_kbd, F_GETFL);
		if (fl == -1) {
			printf("fcntl(GETFL): %s\n", STRERR);
			return -1;
		}
		if (fcntl(ascii_kbd, F_SETFL, fl | O_NONBLOCK) == -1) {
			printf("fcntl(SETFL, O_NONBLOCK): %s\n", STRERR);
			return -1;
		}

		ev.events = EPOLLIN;
		ev.data.fd = ascii_kbd;
		if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, ascii_kbd, &ev)) {
			printf("epoll_ctl(add): %s\n", STRERR);
			return -1;
		}
		/*if (tcsetpgrp(ascii_kbd, getpgid(0)) == -1) {
			printf("tcsetpgrp: %s\n", STRERR);
			return -1;
		}*/
		dev = calloc(1, sizeof(struct input_device));
		if (dev == NULL)
			return -1;
		snprintf(dev->name, sizeof(dev->name), "ascii_kbd");
		dev->fd = ascii_kbd;
		dev->func_read = ascii_kbd_read;
		dev->next = g_input_devices;
		g_input_devices = dev;

	}
	if (evdev_kbd != -1) {
		 /* TODO detect default keyboard by checking device
		  * with most keys i guess? also add an environment variable,
		  * maybe permission fle. for now it's hardcoded to event1
		  * as kbd device :P
		  */
		struct input_device *dev;
		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLIN;
		ev.data.fd = evdev_kbd;
		if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, evdev_kbd, &ev)) {
			printf("epoll_ctl(add): %s\n", STRERR);
			return -1;
		}
		dev = calloc(1, sizeof(struct input_device));
		if (dev == NULL)
			return -1;
		snprintf(dev->name, sizeof(dev->name), "evdev_kbd");
		dev->fd = evdev_kbd;
		dev->func_read = evdev_read;
		dev->next = g_input_devices;
		g_input_devices = dev;

		/* TODO keymap loading? i think udev is usually responsible for this,
		 * but it looks like you can't even do this for USB keyboards?
		 * evdev_print_keycodes(dev);*/
		printf("evdev keyboard created\n");
	}
	return 0;
}

/* TODO move this to client */
static int ascii_kbd_is_shifted(unsigned char c)
{
	return (
			(c > 32  && c < 44 && c != 39)
		|| 	(c > 57  && c < 91 && c != 61 && c != 59)
		|| 	(c > 93  && c < 96)
		|| 	(c > 122 && c < 127));
}

/* This is a fallback input mode that computes shift state in a hacky manner,
 * and doesnt support many useful keys like ctrl,alt,capslock,etc
 * we could add raw kbd support too, eventually...
 */
static int ascii_kbd_read(struct input_device *self, int client)
{
	unsigned char buf[1024];
	int i, r;
interrupted:
	r = read(self->fd, buf, sizeof(buf));
	if (r == -1) {
		if (errno == EAGAIN) {
			return 0;
		}
		else if (errno == EINTR) {
			goto interrupted;
		}
		printf("read input: %s\n", STRERR);
		return -1;
	}
	if (client != -1) {
		/* 1 char == 1 keycode */
		for (i = 0; i < r; ++i)
		{
			struct spr16_msgdata_input_keyboard data;
			struct spr16_msghdr hdr;
			hdr.type = SPRITEMSG_INPUT_KEYBOARD;
			data.keycode = buf[i];
			data.flags = ascii_kbd_is_shifted(buf[i]) ? SPR16_KBD_SHIFT : 0;
			if (spr16_write_msg(client, &hdr, &data, sizeof(data))) {
				return (errno == EAGAIN) ? 0 : -1;
			}
		}
	}
	return 0;
}


static int translate_evdev(struct input_device *dev,
			   struct spr16_msgdata_input_keyboard *data)
{
	uint32_t shift = dev->private & SPR16_KBD_SHIFT;
	/* TODO load keycodes from file, but keep this fallback code path */
	switch (data->keycode)
	{

		/* chars > 127
		 * TODO for stdin mode, split into two raw/ascii, raw just
		 * forwards bytes without any processing, ascii can check for
		 * special escape codes to implement shift/ctrl/etc, someday
		 *
		 * TODO still missing some important keys (capslock, numpad, media)
		 */
		case KEY_LEFTSHIFT:  data->keycode = SPR16_KEYCODE_LSHIFT;   break;
		case KEY_RIGHTSHIFT: data->keycode = SPR16_KEYCODE_RSHIFT;   break;
		case KEY_LEFTCTRL:   data->keycode = SPR16_KEYCODE_LCTRL;    break;
		case KEY_RIGHTCTRL:  data->keycode = SPR16_KEYCODE_RCTRL;    break;
		case KEY_LEFTALT:    data->keycode = SPR16_KEYCODE_LALT;     break;
		case KEY_RIGHTALT:   data->keycode = SPR16_KEYCODE_RALT;     break;
		case KEY_UP: 	     data->keycode = SPR16_KEYCODE_UP;       break;
		case KEY_DOWN:       data->keycode = SPR16_KEYCODE_DOWN;     break;
		case KEY_LEFT:       data->keycode = SPR16_KEYCODE_LEFT;     break;
		case KEY_RIGHT:      data->keycode = SPR16_KEYCODE_RIGHT;    break;
		case KEY_PAGEUP:     data->keycode = SPR16_KEYCODE_PAGEUP;   break;
		case KEY_PAGEDOWN:   data->keycode = SPR16_KEYCODE_PAGEDOWN; break;
		case KEY_HOME:       data->keycode = SPR16_KEYCODE_HOME;     break;
		case KEY_END:        data->keycode = SPR16_KEYCODE_END;      break;
		case KEY_INSERT:     data->keycode = SPR16_KEYCODE_INSERT;   break;
		case KEY_DELETE:     data->keycode = SPR16_KEYCODE_DELETE;   break;

		case KEY_F1:  data->keycode = SPR16_KEYCODE_F1;  break;
		case KEY_F2:  data->keycode = SPR16_KEYCODE_F2;  break;
		case KEY_F3:  data->keycode = SPR16_KEYCODE_F3;  break;
		case KEY_F4:  data->keycode = SPR16_KEYCODE_F4;  break;
		case KEY_F5:  data->keycode = SPR16_KEYCODE_F5;  break;
		case KEY_F6:  data->keycode = SPR16_KEYCODE_F6;  break;
		case KEY_F7:  data->keycode = SPR16_KEYCODE_F7;  break;
		case KEY_F8:  data->keycode = SPR16_KEYCODE_F8;  break;
		case KEY_F9:  data->keycode = SPR16_KEYCODE_F9;  break;
		case KEY_F10: data->keycode = SPR16_KEYCODE_F10; break;
		case KEY_F11: data->keycode = SPR16_KEYCODE_F11; break;
		case KEY_F12: data->keycode = SPR16_KEYCODE_F12; break;
		case KEY_F13: data->keycode = SPR16_KEYCODE_F13; break;
		case KEY_F14: data->keycode = SPR16_KEYCODE_F14; break;
		case KEY_F15: data->keycode = SPR16_KEYCODE_F15; break;
		case KEY_F16: data->keycode = SPR16_KEYCODE_F16; break;
		case KEY_F17: data->keycode = SPR16_KEYCODE_F17; break;
		case KEY_F18: data->keycode = SPR16_KEYCODE_F18; break;
		case KEY_F19: data->keycode = SPR16_KEYCODE_F19; break;
		case KEY_F20: data->keycode = SPR16_KEYCODE_F20; break;
		case KEY_F21: data->keycode = SPR16_KEYCODE_F21; break;
		case KEY_F22: data->keycode = SPR16_KEYCODE_F22; break;
		case KEY_F23: data->keycode = SPR16_KEYCODE_F23; break;
		case KEY_F24: data->keycode = SPR16_KEYCODE_F24; break;

		/*case KEY_KPASTERISK: data->keycode =  break;*/
		/*case KEY_CAPSLOCK:   data->keycode = shift ? '' : ''; break;*/

		/* chars <= 127 */
		case KEY_ESC: data->keycode = 27; break;  /* ascii escape code */
		case KEY_0: data->keycode = shift ? ')' : '0'; break;
		case KEY_1: data->keycode = shift ? '!' : '1'; break;
		case KEY_2: data->keycode = shift ? '@' : '2'; break;
		case KEY_3: data->keycode = shift ? '#' : '3'; break;
		case KEY_4: data->keycode = shift ? '$' : '4'; break;
		case KEY_5: data->keycode = shift ? '%' : '5'; break;
		case KEY_6: data->keycode = shift ? '^' : '6'; break;
		case KEY_7: data->keycode = shift ? '&' : '7'; break;
		case KEY_8: data->keycode = shift ? '*' : '8'; break;
		case KEY_9: data->keycode = shift ? '(' : '9'; break;

		case KEY_MINUS:      data->keycode = shift ? '_'  : '-' ; break;
		case KEY_EQUAL:      data->keycode = shift ? '+'  : '=' ; break;
		case KEY_BACKSPACE:  data->keycode = shift ? '\b' : '\b'; break;
		case KEY_TAB:        data->keycode = shift ? '\t' : '\t'; break;
		case KEY_LEFTBRACE:  data->keycode = shift ? '{'  : '[' ; break;
		case KEY_RIGHTBRACE: data->keycode = shift ? '}'  : ']' ; break;
		case KEY_ENTER:      data->keycode = shift ? '\n' : '\n'; break;
		case KEY_SEMICOLON:  data->keycode = shift ? ':'  : ';' ; break;
		case KEY_APOSTROPHE: data->keycode = shift ? '"'  : '\''; break;
		case KEY_GRAVE:      data->keycode = shift ? '~'  : '`' ; break;
		case KEY_BACKSLASH:  data->keycode = shift ? '|'  : '\\'; break;
		case KEY_COMMA:      data->keycode = shift ? '<'  : ',' ; break;
		case KEY_DOT:        data->keycode = shift ? '>'  : '.' ; break;
		case KEY_SLASH:      data->keycode = shift ? '?'  : '/' ; break;
		case KEY_SPACE:      data->keycode = shift ? ' '  : ' ' ; break;

		case KEY_A: data->keycode = shift ? 'A' : 'a'; break;
		case KEY_B: data->keycode = shift ? 'B' : 'b'; break;
		case KEY_C: data->keycode = shift ? 'C' : 'c'; break;
		case KEY_D: data->keycode = shift ? 'D' : 'd'; break;
		case KEY_E: data->keycode = shift ? 'E' : 'e'; break;
		case KEY_F: data->keycode = shift ? 'F' : 'f'; break;
		case KEY_G: data->keycode = shift ? 'G' : 'g'; break;
		case KEY_H: data->keycode = shift ? 'H' : 'h'; break;
		case KEY_I: data->keycode = shift ? 'I' : 'i'; break;
		case KEY_J: data->keycode = shift ? 'J' : 'j'; break;
		case KEY_K: data->keycode = shift ? 'K' : 'k'; break;
		case KEY_L: data->keycode = shift ? 'L' : 'l'; break;
		case KEY_M: data->keycode = shift ? 'M' : 'm'; break;
		case KEY_N: data->keycode = shift ? 'N' : 'n'; break;
		case KEY_O: data->keycode = shift ? 'O' : 'o'; break;
		case KEY_P: data->keycode = shift ? 'P' : 'p'; break;
		case KEY_Q: data->keycode = shift ? 'Q' : 'q'; break;
		case KEY_R: data->keycode = shift ? 'R' : 'r'; break;
		case KEY_S: data->keycode = shift ? 'S' : 's'; break;
		case KEY_T: data->keycode = shift ? 'T' : 't'; break;
		case KEY_U: data->keycode = shift ? 'U' : 'u'; break;
		case KEY_V: data->keycode = shift ? 'V' : 'v'; break;
		case KEY_W: data->keycode = shift ? 'W' : 'w'; break;
		case KEY_X: data->keycode = shift ? 'X' : 'x'; break;
		case KEY_Y: data->keycode = shift ? 'Y' : 'Y'; break;
		case KEY_Z: data->keycode = shift ? 'Z' : 'z'; break;

		default: return -1;
	}
	return 0;
}

/* TODO SYN_DROPPED!! also hardware repeats are getting ignored by Xorg */
#define EV_BUF ((EV_MAX+1)*2)
static int evdev_read(struct input_device *self, int client)
{
	struct input_event events[EV_BUF];
	unsigned int i;
	int r;

interrupted:
	r = read(self->fd, events, sizeof(events));
	if (r == -1) {
		if (errno == EAGAIN) {
			return 0;
		}
		else if (errno == EINTR) {
			goto interrupted;
		}
		printf("read input: %s\n", STRERR);
		return -1;
	}
	if (client == -1)
		return 0; /* TODO this is prolly wrong, we should continue to check
			     for SYN_DROPPED and change state if needed */
	if (r < (int)sizeof(struct input_event)
			|| r % (int)sizeof(struct input_event)) {
		return -1;
	}
	for (i = 0; i < r / sizeof(struct input_event); ++i)
	{
		struct spr16_msgdata_input_keyboard data;
		struct spr16_msghdr hdr;
		struct input_event *event = &events[i];

		/* if we get SYN_DROPPED we will have to enter a state where
		 * we discard everything until the next SYN?? if we encounter
		 * an additional drop, well I suppose it's protocol error??
		 * and some way to actually test this case?
		 */
		if (event->type != EV_KEY /*&& event->type != EV_REP*/)
			continue; /* TODO handle syn,  led, etc. */
		hdr.type = SPRITEMSG_INPUT_KEYBOARD;
		data.keycode = event->code;/*'A'*//*buf[i];*/
		data.flags = 0;
		if (translate_evdev(self, &data)) {
			continue;
		}
		if (!event->value)
			data.flags |= SPR16_INPUT_RELEASE;
		if (spr16_write_msg(client, &hdr, &data, sizeof(data))) {
			return (errno == EAGAIN) ? 0 : -1;
		}
	}
	return 0;
}

/*
 *  find the input fd and translate events
 */
static int input_events(int evfd)
{
	struct input_device *device;
	int cl_fd;

	if (g_focused_client == NULL)
		g_focused_client = g_clients;
	cl_fd = g_focused_client ? g_focused_client->socket : -1;

	for (device = g_input_devices; device; device = device->next)
	{
		if (evfd != device->fd) {
			continue;
		}
		if (device->func_read(device, cl_fd)) {
			return -1;
		}
		return 1;
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
		printf("got new socket: %d\n", newsock);
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
		return -1;
	}
	if (!g_state.sfb || !g_state.sfb->addr || !cl->sprite.shmem.addr) {
		printf("bad ptr %p \n", (void *)cl->sprite.shmem.addr);
		return -1;
	}

	/* sync box regions TODO sse/neon/etc */
	for (i = 0; i < region->height; ++i)
	{
		uint32_t xoff  = region->x * weight;
		uint32_t svoff = (((region->y + i) * g_width) * weight)+xoff;
		uint32_t cloff = (((region->y + i) * cl->sprite.width) * weight)+xoff;
		memcpy(g_state.sfb->addr + svoff,
		       cl->sprite.shmem.addr + cloff,
		       region->width * weight);
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
		int r = input_events(evfd);
		if (r == 1) { /* input event */
			continue;
		}
		else if (r == -1) { /* input error */
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

