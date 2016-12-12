/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * TODO EVIOCGRAB, prevent others from reading our input?
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <linux/input.h>
#include <dirent.h>
#include <signal.h>
#include <termios.h>

#include "../protocol/spr16.h"

#define STRERR strerror(errno)

/* number of longs needed to represent n bits. */
#define NLONGS(n) ((n + (sizeof(long) * 8)) - 1 / sizeof(long))

const char devpfx[] = "event";
const char devdir[] = "/dev/input";

int generic_flush(struct input_device *self)
{
	tcflush(self->fd, TCIFLUSH);
	while (1)
	{
		char buf[1024];
		fd_set fds;
		struct timeval t;
		int r;
		t.tv_sec = 0;
		t.tv_usec = 0;
intr:
		FD_ZERO(&fds);
		FD_SET(self->fd, &fds);
		r = select(self->fd + 1, &fds, NULL, NULL, &t);
		if (r == -1) {
			if (errno == EINTR)
				goto intr;
			printf("generic_flush select: %s\n", STRERR);
			return -1;
		}
		if (r == 0)
			return 0;
		r = read(self->fd, buf, sizeof(buf));
		if (r == -1) {
			if (errno == EINTR)
				goto intr;
			else if (errno == EAGAIN)
				return 0;
			return -1;
		}
		if (r == 0)
			return 0;
	}
	return -1;
}

/* This is a fallback input mode that computes shift state in a hacky manner,
 * and doesnt support many useful keys like ctrl,alt,capslock,etc
 * we could add raw kbd support too, eventually...
 */
static int read_ascii(struct input_device *self, int client)
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
			struct spr16_msgdata_input data;
			struct spr16_msghdr hdr;
			hdr.type = SPRITEMSG_INPUT;
			data.code = buf[i];
			data.type = SPR16_INPUT_KEY_ASCII;
			data.bits = 0;
			if (spr16_write_msg(client, &hdr, &data, sizeof(data))) {
				return (errno == EAGAIN) ? 0 : -1;
			}
		}
	}
	return 0;
}

static int read_raw(struct input_device *self, int client)
{
	/* TODO */
	(void)self;
	(void)client;
	return -1;
}

static int evdev_translate_keycode(struct input_device *dev,
				   struct spr16_msgdata_input *data)
{
	uint32_t shift = 0;
	(void)dev;
	switch (data->code)
	{
		/* chars > 127
		 * TODO still missing some important keys 
		 */
		case KEY_CAPSLOCK:   data->code = SPR16_KEYCODE_CAPSLOCK; break;
		case KEY_LEFTSHIFT:  data->code = SPR16_KEYCODE_LSHIFT;   break;
		case KEY_RIGHTSHIFT: data->code = SPR16_KEYCODE_RSHIFT;   break;
		case KEY_LEFTCTRL:   data->code = SPR16_KEYCODE_LCTRL;    break;
		case KEY_RIGHTCTRL:  data->code = SPR16_KEYCODE_RCTRL;    break;
		case KEY_LEFTALT:    data->code = SPR16_KEYCODE_LALT;     break;
		case KEY_RIGHTALT:   data->code = SPR16_KEYCODE_RALT;     break;
		case KEY_UP: 	     data->code = SPR16_KEYCODE_UP;       break;
		case KEY_DOWN:       data->code = SPR16_KEYCODE_DOWN;     break;
		case KEY_LEFT:       data->code = SPR16_KEYCODE_LEFT;     break;
		case KEY_RIGHT:      data->code = SPR16_KEYCODE_RIGHT;    break;
		case KEY_PAGEUP:     data->code = SPR16_KEYCODE_PAGEUP;   break;
		case KEY_PAGEDOWN:   data->code = SPR16_KEYCODE_PAGEDOWN; break;
		case KEY_HOME:       data->code = SPR16_KEYCODE_HOME;     break;
		case KEY_END:        data->code = SPR16_KEYCODE_END;      break;
		case KEY_INSERT:     data->code = SPR16_KEYCODE_INSERT;   break;
		case KEY_DELETE:     data->code = SPR16_KEYCODE_DELETE;   break;

		case KEY_F1:  data->code = SPR16_KEYCODE_F1;  break;
		case KEY_F2:  data->code = SPR16_KEYCODE_F2;  break;
		case KEY_F3:  data->code = SPR16_KEYCODE_F3;  break;
		case KEY_F4:  data->code = SPR16_KEYCODE_F4;  break;
		case KEY_F5:  data->code = SPR16_KEYCODE_F5;  break;
		case KEY_F6:  data->code = SPR16_KEYCODE_F6;  break;
		case KEY_F7:  data->code = SPR16_KEYCODE_F7;  break;
		case KEY_F8:  data->code = SPR16_KEYCODE_F8;  break;
		case KEY_F9:  data->code = SPR16_KEYCODE_F9;  break;
		case KEY_F10: data->code = SPR16_KEYCODE_F10; break;
		case KEY_F11: data->code = SPR16_KEYCODE_F11; break;
		case KEY_F12: data->code = SPR16_KEYCODE_F12; break;
		case KEY_F13: data->code = SPR16_KEYCODE_F13; break;
		case KEY_F14: data->code = SPR16_KEYCODE_F14; break;
		case KEY_F15: data->code = SPR16_KEYCODE_F15; break;
		case KEY_F16: data->code = SPR16_KEYCODE_F16; break;
		case KEY_F17: data->code = SPR16_KEYCODE_F17; break;
		case KEY_F18: data->code = SPR16_KEYCODE_F18; break;
		case KEY_F19: data->code = SPR16_KEYCODE_F19; break;
		case KEY_F20: data->code = SPR16_KEYCODE_F20; break;
		case KEY_F21: data->code = SPR16_KEYCODE_F21; break;
		case KEY_F22: data->code = SPR16_KEYCODE_F22; break;
		case KEY_F23: data->code = SPR16_KEYCODE_F23; break;
		case KEY_F24: data->code = SPR16_KEYCODE_F24; break;

		/*case KEY_KPASTERISK: data->code =  break;*/
		/*case KEY_CAPSLOCK:   data->code = shift ? '' : ''; break;*/


		/* mouse buttons */
		case BTN_MOUSE:   data->code = SPR16_KEYCODE_LBTN; break;
		case BTN_RIGHT:   data->code = SPR16_KEYCODE_RBTN; break;
		case BTN_MIDDLE:  data->code = SPR16_KEYCODE_ABTN; break;
		case BTN_SIDE:    data->code = SPR16_KEYCODE_BBTN; break;
		case BTN_EXTRA:   data->code = SPR16_KEYCODE_CBTN; break;
		case BTN_FORWARD: data->code = SPR16_KEYCODE_DBTN; break;
		case BTN_BACK:    data->code = SPR16_KEYCODE_EBTN; break;
		case BTN_TASK:    data->code = SPR16_KEYCODE_FBTN; break;


		/* chars <= 127 */
		case KEY_ESC: data->code = 27; break;  /* ascii escape code */
		case KEY_0: data->code = shift ? ')' : '0'; break;
		case KEY_1: data->code = shift ? '!' : '1'; break;
		case KEY_2: data->code = shift ? '@' : '2'; break;
		case KEY_3: data->code = shift ? '#' : '3'; break;
		case KEY_4: data->code = shift ? '$' : '4'; break;
		case KEY_5: data->code = shift ? '%' : '5'; break;
		case KEY_6: data->code = shift ? '^' : '6'; break;
		case KEY_7: data->code = shift ? '&' : '7'; break;
		case KEY_8: data->code = shift ? '*' : '8'; break;
		case KEY_9: data->code = shift ? '(' : '9'; break;

		case KEY_MINUS:      data->code = shift ? '_'  : '-' ; break;
		case KEY_EQUAL:      data->code = shift ? '+'  : '=' ; break;
		case KEY_BACKSPACE:  data->code = shift ? '\b' : '\b'; break;
		case KEY_TAB:        data->code = shift ? '\t' : '\t'; break;
		case KEY_LEFTBRACE:  data->code = shift ? '{'  : '[' ; break;
		case KEY_RIGHTBRACE: data->code = shift ? '}'  : ']' ; break;
		case KEY_ENTER:      data->code = shift ? '\n' : '\n'; break;
		case KEY_SEMICOLON:  data->code = shift ? ':'  : ';' ; break;
		case KEY_APOSTROPHE: data->code = shift ? '"'  : '\''; break;
		case KEY_GRAVE:      data->code = shift ? '~'  : '`' ; break;
		case KEY_BACKSLASH:  data->code = shift ? '|'  : '\\'; break;
		case KEY_COMMA:      data->code = shift ? '<'  : ',' ; break;
		case KEY_DOT:        data->code = shift ? '>'  : '.' ; break;
		case KEY_SLASH:      data->code = shift ? '?'  : '/' ; break;
		case KEY_SPACE:      data->code = shift ? ' '  : ' ' ; break;

		case KEY_A: data->code = shift ? 'A' : 'a'; break;
		case KEY_B: data->code = shift ? 'B' : 'b'; break;
		case KEY_C: data->code = shift ? 'C' : 'c'; break;
		case KEY_D: data->code = shift ? 'D' : 'd'; break;
		case KEY_E: data->code = shift ? 'E' : 'e'; break;
		case KEY_F: data->code = shift ? 'F' : 'f'; break;
		case KEY_G: data->code = shift ? 'G' : 'g'; break;
		case KEY_H: data->code = shift ? 'H' : 'h'; break;
		case KEY_I: data->code = shift ? 'I' : 'i'; break;
		case KEY_J: data->code = shift ? 'J' : 'j'; break;
		case KEY_K: data->code = shift ? 'K' : 'k'; break;
		case KEY_L: data->code = shift ? 'L' : 'l'; break;
		case KEY_M: data->code = shift ? 'M' : 'm'; break;
		case KEY_N: data->code = shift ? 'N' : 'n'; break;
		case KEY_O: data->code = shift ? 'O' : 'o'; break;
		case KEY_P: data->code = shift ? 'P' : 'p'; break;
		case KEY_Q: data->code = shift ? 'Q' : 'q'; break;
		case KEY_R: data->code = shift ? 'R' : 'r'; break;
		case KEY_S: data->code = shift ? 'S' : 's'; break;
		case KEY_T: data->code = shift ? 'T' : 't'; break;
		case KEY_U: data->code = shift ? 'U' : 'u'; break;
		case KEY_V: data->code = shift ? 'V' : 'v'; break;
		case KEY_W: data->code = shift ? 'W' : 'w'; break;
		case KEY_X: data->code = shift ? 'X' : 'x'; break;
		case KEY_Y: data->code = shift ? 'Y' : 'Y'; break;
		case KEY_Z: data->code = shift ? 'Z' : 'z'; break;

		default: return -1;
	}
	return 0;
}

/*
 *  prevent vt switching keys from being sent
 *  raise sigterm on ctrl-alt-escape
 */
static int preempt_keycodes(struct input_device *self, struct spr16_msgdata_input *msg)
{
	switch (msg->code)
	{
	case SPR16_KEYCODE_F1:
	case SPR16_KEYCODE_F2:
	case SPR16_KEYCODE_F3:
	case SPR16_KEYCODE_F4:
	case SPR16_KEYCODE_F5:
	case SPR16_KEYCODE_F6:
	case SPR16_KEYCODE_F7:
	case SPR16_KEYCODE_F8:
	case SPR16_KEYCODE_F9:
	case SPR16_KEYCODE_F10:
	case SPR16_KEYCODE_F11:
	case SPR16_KEYCODE_F12:
	case SPR16_KEYCODE_F13:
	case SPR16_KEYCODE_F14:
	case SPR16_KEYCODE_F15:
	case SPR16_KEYCODE_F16:
	case SPR16_KEYCODE_F17:
	case SPR16_KEYCODE_F18:
	case SPR16_KEYCODE_F19:
	case SPR16_KEYCODE_F20:
	case SPR16_KEYCODE_F21:
	case SPR16_KEYCODE_F22:
	case SPR16_KEYCODE_F23:
	case SPR16_KEYCODE_F24:
	case SPR16_KEYCODE_LEFT:
	case SPR16_KEYCODE_RIGHT:
		if (self->keyflags & SPR16_KEYMOD_LALT) {
			return 1;
		}
		break;
	case SPR16_KEYCODE_ESCAPE:
		if ((self->keyflags & (SPR16_KEYMOD_LALT | SPR16_KEYMOD_LCTRL))
				   == (SPR16_KEYMOD_LALT | SPR16_KEYMOD_LCTRL)) {
			printf("abrupt screen shutdown\n");
			raise(SIGTERM);
			return 1;
		}
		break;

	case SPR16_KEYCODE_LALT:
		if (msg->val == 0) { /* down */
			if (self->keyflags & SPR16_KEYMOD_LALT)
				self->keyflags &= ~SPR16_KEYMOD_LALT;
		}
		else if (msg->val == 1) { /* up */
			self->keyflags |= SPR16_KEYMOD_LALT;
		}
		break;
	case SPR16_KEYCODE_LCTRL:
		if (msg->val == 0) { /* down */
			if (self->keyflags & SPR16_KEYMOD_LCTRL)
				self->keyflags &= ~SPR16_KEYMOD_LCTRL;
		}
		else if (msg->val == 1) { /* up */
			self->keyflags |= SPR16_KEYMOD_LCTRL;
		}
		break;

	default:
		break;
	}
	return 0;
}


/* TODO SYN_DROPPED!! also hardware repeats are getting ignored by Xorg
 * also, we probably want to EVIOCGRAB, is that essentially locking the device?
 * */
#define EV_BUF ((EV_MAX+1)*2)
static int read_evdev(struct input_device *self, int client)
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
	if (r < (int)sizeof(struct input_event)
			|| r % (int)sizeof(struct input_event)) {
		return -1;
	}
	for (i = 0; i < r / sizeof(struct input_event); ++i)
	{
		struct spr16_msgdata_input data;
		struct spr16_msghdr hdr;
		struct input_event *event = &events[i];
		memset(&data, 0, sizeof(data));
		/* if we get SYN_DROPPED we will have to enter a state where
		 * we discard everything until the next SYN?? if we encounter
		 * an additional drop, well I suppose it's protocol error??
		 * and some way to actually test this case?
		 */
		/* TODO handle syn, abs, etc. */
		hdr.type = SPRITEMSG_INPUT;
		switch (event->type)
		{
		case EV_KEY:
			data.code = event->code;
			data.type = SPR16_INPUT_KEY;
			data.val  = event->value;
			if (evdev_translate_keycode(self, &data)) {
				continue;
			}
			if (preempt_keycodes(self, &data))
				continue;

		break;
		case EV_REL:
			data.type = SPR16_INPUT_AXIS_RELATIVE;
			data.code = event->code;
			data.val  = event->value;
		break;
		default:
			continue;
		}

		if (client == -1)
			return 0; /* TODO this is prolly wrong, we should continue to check
				     for SYN_DROPPED and change state if needed */
		if (spr16_write_msg(client, &hdr, &data, sizeof(data))) {
			return (errno == EAGAIN) ? 0 : -1;
		}
	}
	return 0;
}

/*
 * device capability classes
 */
enum {
	CAP_KEYBOARD = 1,
	CAP_MOUSE,
	CAP_AXIS,
	CAP_TOUCH,
	CAP_FEEDBACK
};

static int bit_count(unsigned long bits[], unsigned int nlongs)
{
	int count = 0;
	unsigned int i, z;
	for (i = 0; i < nlongs; ++i)
	{
		for (z = 0; z < sizeof(long) * 8; ++z)
		{
			if (bits[i] & (1 << z))
				++count;
		}
	}
	return count;
}


/*
 * may not always get you the right device, use env var to specify exact device
 * export EVDEV_KEYBOARD=/dev/input/event1
 * export EVDEV_MOUSE=/dev/input/event0
 * etc...
 */
struct prospective_path {
	char path[128];
	unsigned int key, rel, abs, led, ff;
} g_prospect;
static void select_prp(struct prospective_path *p, char *path, unsigned int key,
		unsigned int rel, unsigned int abs, unsigned int ff)
{
	snprintf(p->path, sizeof(p->path), "%s", path);
	p->key = key;
	p->rel = rel;
	p->abs = abs;
	p->ff  = ff;
}

static struct prospective_path *check_environ(int cap_class)
{
	char *e = NULL;
	memset(&g_prospect, 0, sizeof(g_prospect));
	switch (cap_class)
	{
	case CAP_KEYBOARD:
		e = getenv("EVDEV_KEYBOARD");
		break;
	case CAP_MOUSE:
		e = getenv("EVDEV_MOUSE");
		break;
	default:
		return NULL;
	}
	if (e == NULL)
		return NULL;
	if (strncmp(e, "event", 5) != 0)
		return NULL;
	snprintf(g_prospect.path, sizeof(g_prospect.path), "%s/%s", devdir, e);
	return &g_prospect;
}

static struct prospective_path *find_most_capable(int cap_class)
{
	/*unsigned long evbits[NLONGS(EV_CNT)];*/
	unsigned long keybits[NLONGS(KEY_CNT)];
	unsigned long relbits[NLONGS(REL_CNT)];
	unsigned long absbits[NLONGS(ABS_CNT)];
	unsigned long ledbits[NLONGS(LED_CNT)];
	unsigned long ffbits[NLONGS(FF_CNT)];
	unsigned int key = 0;
	unsigned int rel = 0;
	unsigned int abs = 0;
	unsigned int led = 0;
	unsigned int ff  = 0;
	struct dirent *dent;
	DIR *dir;
	int devfd = -1;

	memset(&g_prospect, 0, sizeof(g_prospect));
	memset(keybits, 0, sizeof(keybits));
	memset(relbits, 0, sizeof(relbits));
	memset(absbits, 0, sizeof(absbits));
	memset(ledbits, 0, sizeof(ledbits));
	memset(ffbits,  0, sizeof(ffbits));

	dir = opendir(devdir);
	if (dir == NULL) {
		printf("couldn't open %s: %s\n", devdir, STRERR);
		return NULL;
	}
	while ((dent = readdir(dir)))
	{
		char path[128];

		if (strncmp(dent->d_name, devpfx, strlen(devpfx)) != 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", devdir, dent->d_name);

		devfd = open(path, O_RDONLY);
		if (devfd == -1)
			goto err_close;
		if (ioctl(devfd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) == -1)
			goto err_close;
		if (ioctl(devfd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits) == -1)
			goto err_close;
		if (ioctl(devfd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) == -1)
			goto err_close;
		if (ioctl(devfd, EVIOCGBIT(EV_LED, sizeof(ledbits)), ledbits) == -1)
			goto err_close;
		if (ioctl(devfd, EVIOCGBIT(EV_FF,  sizeof(ffbits)),  ffbits) == -1)
			goto err_close;
		key = bit_count(keybits, NLONGS(KEY_CNT));
		abs = bit_count(absbits, NLONGS(ABS_CNT));
		rel = bit_count(relbits, NLONGS(REL_CNT));
		led = bit_count(ledbits, NLONGS(LED_CNT));
		ff  = bit_count(ffbits,  NLONGS(FF_CNT));
		switch (cap_class)
		{
		case CAP_KEYBOARD:
			if (led > g_prospect.led) {
				/* some keyboards end up making multiple event files
				 * don't send correct events..... device with LEDs is
				 * hopefully the real keyboard, and not clone */
				select_prp(&g_prospect, path, key, abs, rel, ff);
			}
			if (key > g_prospect.key) {
				select_prp(&g_prospect, path, key, abs, rel, ff);
			}
			else if (key == g_prospect.key) {
				if (rel > g_prospect.rel) {
					select_prp(&g_prospect, path, key, abs, rel, ff);
				}
				else if (abs > g_prospect.abs) {
					select_prp(&g_prospect, path, key, abs, rel, ff);
				}
				else if (ff > g_prospect.ff) {
					select_prp(&g_prospect, path, key, abs, rel, ff);
				}
			}
			break;
		case CAP_MOUSE:
			if (g_prospect.key == 0 && rel >= 2)
				select_prp(&g_prospect, path, key, abs, rel, ff);
			else if (rel >= 2 && rel > g_prospect.rel)
				select_prp(&g_prospect, path, key, abs, rel, ff);
			break;
		case CAP_AXIS:
			break;
		case CAP_TOUCH:
			break;
		case CAP_FEEDBACK:
			break;
		default:
			errno = EINVAL;
			goto err_close;
		}
		close(devfd);
	}

	closedir(dir);
	if (g_prospect.path[0] == '\0')
		return NULL;
	return &g_prospect;
err_close:
	printf("evdev error: %s\n", STRERR);
	close(devfd);
	closedir(dir);
	return NULL;
}

/*
 * TODO, check for duplicates, EVIOCGRAB or use owner/group
 */
static int evdev_instantiate(struct input_device **device_list,
				int epoll_fd, char *devpath)
{
	struct epoll_event ev;
	struct input_device *dev;
	char devname[64];
	int devfd;
	devfd = open(devpath, O_RDONLY);
	if (devfd == -1) {
		printf("open(%s): %s\n", devpath, STRERR);
		return -1;
	}
	if (ioctl(devfd, EVIOCGNAME(sizeof(devname)-1), devname) == -1) {
		printf("evdev get name: %s\n", STRERR);
		return -1;
	}
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = devfd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, devfd, &ev)) {
		printf("epoll_ctl(add): %s\n", STRERR);
		return -1;
	}
	dev = calloc(1, sizeof(struct input_device));
	if (dev == NULL)
		return -1;
	snprintf(dev->name, sizeof(dev->name), "%s", devname);
	snprintf(dev->path, sizeof(dev->path), "%s", devpath);
	dev->fd = devfd;
	dev->func_read  = read_evdev;
	dev->func_flush = generic_flush;
	dev->next = *device_list;
	*device_list = dev;
	return 0;
}


static void load_stream(struct input_device **device_list, int epoll_fd,
			int streamfd, int mode)
{
	struct epoll_event ev;
	struct input_device *dev;
	int fl;

	memset(&ev, 0, sizeof(ev));
	/* set to nonblocking */
	fl = fcntl(streamfd, F_GETFL);
	if (fl == -1) {
		printf("fcntl(GETFL): %s\n", STRERR);
		return;
	}
	if (fcntl(streamfd, F_SETFL, fl | O_NONBLOCK) == -1) {
		printf("fcntl(SETFL, O_NONBLOCK): %s\n", STRERR);
		return;
	}

	ev.events = EPOLLIN;
	ev.data.fd = streamfd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, streamfd, &ev)) {
		printf("epoll_ctl(add): %s\n", STRERR);
		return;
	}
	/*if (tcsetpgrp(streamfd, getpgid(0)) == -1) {
		printf("tcsetpgrp: %s\n", STRERR);
		return -1;
	}*/
	dev = calloc(1, sizeof(struct input_device));
	if (dev == NULL)
		return;
	dev->fd = streamfd;
	if (mode == 1) {
		snprintf(dev->name, sizeof(dev->name), "ascii-stream");
		dev->func_read  = read_ascii;
		dev->func_flush = generic_flush;
	}
	else {
		snprintf(dev->name, sizeof(dev->name), "raw-stream");
		dev->func_read  = read_raw;
		dev->func_flush = generic_flush;
	}
	dev->next = *device_list;
	*device_list = dev;
}

void load_linux_input_drivers(struct input_device **device_list,
		int epoll_fd, int stdin_mode, int evdev)
{

	/* ascii */
	if (stdin_mode == 1) {
		load_stream(device_list, epoll_fd, STDIN_FILENO, 1);
	} /* raw */
	else if (stdin_mode == 2) {
		load_stream(device_list, epoll_fd, STDIN_FILENO, 2);
	}

	if (evdev) {
		struct prospective_path *prp;

		prp = check_environ(CAP_KEYBOARD);
		if (prp == NULL) {
			prp = find_most_capable(CAP_KEYBOARD);
		}
		if (prp) {
			if (evdev_instantiate(device_list, epoll_fd, prp->path)) {
				printf("evdev instantiate kbd failed %s\n", prp->path);
			}
			else {
				printf("using keyboard: %s\n", prp->path);
			}
		}

		prp = check_environ(CAP_MOUSE);
		if (prp == NULL) {
			prp = find_most_capable(CAP_MOUSE);
		}
		if (prp) {
			if (evdev_instantiate(device_list, epoll_fd, prp->path)) {
				printf("evdev instantiate mouse failed %s\n", prp->path);
			}
			else {
				printf("using mouse: %s\n", prp->path);
			}
		}
	}
}



/* arggh this doesn't work with my USB keyboard :\ */
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
		/* theres a _V2 for 256bit scancode, for ultra precision? */
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





