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
 * TODO EVIOCGRAB, prevent others from reading our input?
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <linux/input.h>
#include <dirent.h>
#include <signal.h>
#include <termios.h>

#include "../../spr16.h"
#include "../../defines.h"
#include "../../screen.h"
#include "../fdpoll-handler.h"
#include "platform.h"

#define STRERR strerror(errno)

#define CURVE_SCALE   1000.0f

const char devpfx[] = "event";
const char devdir[] = "/dev/input";

#define TAP_DELAY  210000 /* microseconds between last tap up, for tap click */
#define TAP_STABL  300000 /* delay permitted from last big motion */

extern struct server_options g_srv_opts;
extern sig_atomic_t g_input_muted; /* don't forward input if muted */
extern sig_atomic_t g_unmute_input;

/* high level input device classification */
enum {
	SPR16_DEV_KEYBOARD = 1,
	SPR16_DEV_MOUSE,
	SPR16_DEV_TOUCH
};
struct drv_evdev_pvt {
	/* surface contacts */
	int active_contact;
	int contact_ids[SPR16_SURFACE_MAX_CONTACTS];
	int contact_x[SPR16_SURFACE_MAX_CONTACTS];
	int contact_y[SPR16_SURFACE_MAX_CONTACTS];
	int contact_mag[SPR16_SURFACE_MAX_CONTACTS];
	int contact_xmax;
	int contact_ymax;
	int contact_magmax;
	int is_mt_surface;
	int has_pressure;

	int invert_x;
	int invert_y;

	/* touchpad hw button events, -1 if no event */
	int touch_lbtn;
	int touch_rbtn;
	int touch_mbtn;
	int touch_sbtn;

	/* trackpad emulation, convert abs_xy to relative with tap to click */
	int is_trackpad;
	int tap_check;
	int tap_reacquire;
	float track_x;
	float track_y;
	float tap_up_x;
	float tap_up_y;
	struct timespec curtime;
	struct timespec last_tap_up;
	struct timespec last_bigmotion;

	/* accelerate x/y pointer (TODO arbitrary axis/devices) */
	unsigned int relative_accel;
	unsigned int vscroll_amount;

	unsigned long evbits[NLONGS(EV_CNT)];
	unsigned long keybits[NLONGS(KEY_CNT)];
	unsigned long relbits[NLONGS(REL_CNT)];
	unsigned long absbits[NLONGS(ABS_CNT)];
	unsigned long ledbits[NLONGS(LED_CNT)];
	unsigned long ffbits[NLONGS(FF_CNT)];
	/* absolute axis / touchpad device parameters */
	struct input_absinfo absinfo[ABS_CNT];
};

struct drv_stream_pvt
{
	int is_ascii;
};

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
		FD_ZERO(&fds);
		FD_SET(self->fd, &fds);
		r = select(self->fd + 1, &fds, NULL, NULL, &t);
		if (r == -1) {
			if (errno == EINTR)
				continue;
			printf("generic_flush select: %s\n", STRERR);
			return -1;
		}
		else if (r == 0)
			return 0;
		r = read(self->fd, buf, sizeof(buf));
		if (r == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		else if (r == 0)
			return 0;
	}
	return -1;
}

static int get_focused_client(struct server_context *ctx)
{

	if (g_input_muted || !ctx->main_screen || !ctx->main_screen->clients) {
		return -1;
	}
	else {
		if (ctx->main_screen->clients->recv_fd_wait)
			return -1;
		return ctx->main_screen->clients->socket;
	}
}

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
static void bit_set(unsigned long *arr, uint32_t bit)
{
	arr[bit / LONG_BITS] |= (1LU << (bit % LONG_BITS));
}
static void bit_clear(unsigned long *arr, uint32_t bit)
{
	arr[bit / LONG_BITS] &= ~(1LU << (bit % LONG_BITS));
}
static int bit_check(unsigned long *arr, uint32_t bit, uint32_t bitcount)
{
	if (bit >= bitcount)
		return 0;
	return !!(arr[bit / LONG_BITS] & (1LU << (bit % LONG_BITS)));
}
static int btn_is_down(struct input_device *self, int keycode)
{
	return bit_check(self->btn_down, keycode, SPR16_KEYCODE_COUNT);
}

int input_flush_all_devices(struct input_device *list)
{
	struct input_device *device;
	int ret = 0;

	/* flush all devices */
	for (device = list; device; device = device->next)
	{

		if (device->fd == -1)
			continue;
		if (device->func_flush(device)) {
			printf("device %s flush failed\n", device->name);
			ret = -1;
		}
		/* reset keydown state */
		memset(device->btn_down, 0, sizeof(device->btn_down));
	}
	return ret;
}

static int update_state(struct server_context *ctx)
{
	if (g_unmute_input) {
		g_unmute_input = 0;
		g_input_muted = 0;

		if (ctx->main_screen && ctx->main_screen->clients)
			spr16_server_reset_client(ctx->main_screen->clients);
		if (input_flush_all_devices(ctx->input_devices))
			return -1;
		server_sync_fullscreen(ctx);
	}
	return 0;
}

/* This is a fallback input mode that computes shift state in a hacky manner,
 * and doesnt support many useful keys like ctrl,alt,capslock,etc
 * we could add raw kbd support too, eventually...
 *
 * TODO track up/down state here too so we can send repeats/keyup
 * keyup is going to require a helper thread to run a timer to send adequately
 * timed events, is it worth all that trouble though?
 *
 * this might be broken have not tested since a refactor
 */
int transceive_ascii(int fd, int event_flags, void *user_data)
{
	unsigned char buf[1024];
	int i, r;
	struct input_device *self = user_data;
	int cl_fd;

	printf("ascii input callback..........\n");
	cl_fd = get_focused_client(self->srv_ctx); /* TODO this doesn't get set yet */
	update_state(self->srv_ctx);

	/* TODO for correctness, loop until EAGAIN */
	(void)event_flags;
interrupted:
	r = read(fd, buf, sizeof(buf));
	if (r == -1) {
		if (errno == EAGAIN) {
			return FDPOLL_HANDLER_OK;
		}
		else if (errno == EINTR) {
			goto interrupted;
		}
		printf("read input: %s\n", STRERR);
		return FDPOLL_HANDLER_REMOVE;
	}
	if (!spr16_server_is_active())
		return FDPOLL_HANDLER_OK;
	if (cl_fd != -1) {
		/* 1 char == 1 keycode */
		for (i = 0; i < r; ++i)
		{
			struct spr16_msgdata_input data;
			struct spr16_msghdr hdr;
			hdr.type = SPRITEMSG_INPUT;
			data.code = buf[i];
			data.type = SPR16_INPUT_KEY_ASCII;
			data.id = self->device_id;
			if (spr16_write_msg(cl_fd, &hdr, &data, sizeof(data))) {
				return FDPOLL_HANDLER_OK;
			}
		}
	}
	return FDPOLL_HANDLER_OK;
}

int transceive_raw(int fd, int event_flags, void *user_data)
{
	struct input_device *self = user_data;
	update_state(self->srv_ctx);
	/* TODO */
	(void)fd;
	(void)event_flags;
	(void)user_data;
	return FDPOLL_HANDLER_REMOVE;
}

static int usecs_elapsed(struct timespec curtime, struct timespec timestamp)
{
	struct timespec elapsed;
	unsigned int usec;

	elapsed.tv_sec  = curtime.tv_sec - timestamp.tv_sec;
	if (!elapsed.tv_sec) {
		elapsed.tv_nsec = curtime.tv_nsec - timestamp.tv_nsec;
		usec = elapsed.tv_nsec / 1000;
	}
	else {
		usec = ((1000000000 - timestamp.tv_nsec) + curtime.tv_nsec) / 1000;
		usec += (elapsed.tv_sec-1) * 1000000;
	}
	return usec;
}

static void trackpad_tap_up(struct drv_evdev_pvt *pvt)
{
	pvt->last_tap_up = pvt->curtime;
	pvt->tap_reacquire = 4;
	pvt->tap_up_x = pvt->track_x;
	pvt->tap_up_y = pvt->track_y;
}

static int set_input_msg(struct input_device *self,
			 struct spr16_msgdata_input *msg,
			 int code)
{
	if (code < 0 || code >= SPR16_KEYCODE_COUNT)
		return -1;

	msg->code = code;

	switch (msg->val)
	{
	case 0: /* up */
		if (!btn_is_down(self, code))
			return -1; /* don't forward event */
		bit_clear(self->btn_down, code);
		break;
	case 1: /* down*/
		bit_set(self->btn_down, code);
		break;
	case 2: /* repeat*/
		if (!btn_is_down(self, code))
			return -1;
		break;
	default:
		return -1;
	}
	return 0;
}

static int evdev_translate_btns(struct input_device *self, struct spr16_msgdata_input *msg)
{
	struct drv_evdev_pvt *pvt = self->private;
	/* for touchpads, should we track button down/up's? probably more
	 * trouble than it's worth considering multitouch tacks on contact id's */
	if (pvt->touch_lbtn == msg->code) {
		msg->code = SPR16_KEYCODE_LBTN;
		return 0;
	}
	else if (pvt->touch_rbtn == msg->code) {
		msg->code = SPR16_KEYCODE_RBTN;
		return 0;
	}
	else if (pvt->touch_mbtn == msg->code) {
		msg->code = SPR16_KEYCODE_ABTN;
		return 0;
	}
	else if (pvt->touch_sbtn == msg->code) {
		if (pvt->is_trackpad) {
			trackpad_tap_up(pvt);
		}
		msg->code = SPR16_KEYCODE_SBTN;
		return 0;
	}

	switch (msg->code)
	{
	/* BTN_MOUSE == BTN_LEFT */
	case BTN_LEFT:    return set_input_msg(self, msg, SPR16_KEYCODE_LBTN);
	case BTN_RIGHT:   return set_input_msg(self, msg, SPR16_KEYCODE_RBTN);
	case BTN_MIDDLE:  return set_input_msg(self, msg, SPR16_KEYCODE_ABTN);
	case BTN_BACK:    return set_input_msg(self, msg, SPR16_KEYCODE_BBTN);
	case BTN_FORWARD: return set_input_msg(self, msg, SPR16_KEYCODE_CBTN);
	case BTN_SIDE:    return set_input_msg(self, msg, SPR16_KEYCODE_DBTN);
	case BTN_EXTRA:   return set_input_msg(self, msg, SPR16_KEYCODE_EBTN);
	case BTN_TASK:    return set_input_msg(self, msg, SPR16_KEYCODE_FBTN);

	case BTN_TOUCH:
		if (!pvt->is_trackpad) {
			msg->code = SPR16_KEYCODE_CONTACT;
			return 0;
		}

		if (msg->val == 0) {
			/* contact up */
			trackpad_tap_up(pvt);
			msg->code = SPR16_KEYCODE_CONTACT;
			return 0;
		}
		else if (usecs_elapsed(pvt->curtime, pvt->last_tap_up)<TAP_DELAY) {
			if (usecs_elapsed(pvt->curtime, pvt->last_bigmotion)<TAP_STABL) {
				return -1;
			}
			/* send contact down later, after receiving updated positions */
			pvt->tap_check = 1;
		}
		break;
	default:
		break;
	}
	return -1;
}

static int evdev_translate_keycode(struct input_device *self,
				   struct spr16_msgdata_input *msg)
{
	uint32_t shift = 0;
	/* TODO still missing some important keys */
	switch (msg->code)
	{
	case KEY_LEFTALT:    return set_input_msg(self, msg, SPR16_KEYCODE_LALT);
	case KEY_LEFTCTRL:   return set_input_msg(self, msg, SPR16_KEYCODE_LCTRL);
	case KEY_CAPSLOCK:   return set_input_msg(self, msg, SPR16_KEYCODE_CAPSLOCK);
	case KEY_LEFTSHIFT:  return set_input_msg(self, msg, SPR16_KEYCODE_LSHIFT);
	case KEY_RIGHTSHIFT: return set_input_msg(self, msg, SPR16_KEYCODE_RSHIFT);
	case KEY_RIGHTCTRL:  return set_input_msg(self, msg, SPR16_KEYCODE_RCTRL);
	case KEY_RIGHTALT:   return set_input_msg(self, msg, SPR16_KEYCODE_RALT);
	case KEY_UP: 	     return set_input_msg(self, msg, SPR16_KEYCODE_UP);
	case KEY_DOWN:       return set_input_msg(self, msg, SPR16_KEYCODE_DOWN);
	case KEY_LEFT:       return set_input_msg(self, msg, SPR16_KEYCODE_LEFT);
	case KEY_RIGHT:      return set_input_msg(self, msg, SPR16_KEYCODE_RIGHT);
	case KEY_PAGEUP:     return set_input_msg(self, msg, SPR16_KEYCODE_PAGEUP);
	case KEY_PAGEDOWN:   return set_input_msg(self, msg, SPR16_KEYCODE_PAGEDOWN);
	case KEY_HOME:       return set_input_msg(self, msg, SPR16_KEYCODE_HOME);
	case KEY_END:        return set_input_msg(self, msg, SPR16_KEYCODE_END);
	case KEY_INSERT:     return set_input_msg(self, msg, SPR16_KEYCODE_INSERT);
	case KEY_DELETE:     return set_input_msg(self, msg, SPR16_KEYCODE_DELETE);
	case KEY_NUMLOCK:    return set_input_msg(self, msg, SPR16_KEYCODE_NUMLOCK);
	case KEY_KP0:        return set_input_msg(self, msg, SPR16_KEYCODE_NP0);
	case KEY_KP1:        return set_input_msg(self, msg, SPR16_KEYCODE_NP1);
	case KEY_KP2:        return set_input_msg(self, msg, SPR16_KEYCODE_NP2);
	case KEY_KP3:        return set_input_msg(self, msg, SPR16_KEYCODE_NP3);
	case KEY_KP4:        return set_input_msg(self, msg, SPR16_KEYCODE_NP4);
	case KEY_KP5:        return set_input_msg(self, msg, SPR16_KEYCODE_NP5);
	case KEY_KP6:        return set_input_msg(self, msg, SPR16_KEYCODE_NP6);
	case KEY_KP7:        return set_input_msg(self, msg, SPR16_KEYCODE_NP7);
	case KEY_KP8:        return set_input_msg(self, msg, SPR16_KEYCODE_NP8);
	case KEY_KP9:        return set_input_msg(self, msg, SPR16_KEYCODE_NP9);
	case KEY_KPSLASH:    return set_input_msg(self, msg, SPR16_KEYCODE_NPSLASH);
	case KEY_KPASTERISK: return set_input_msg(self, msg, SPR16_KEYCODE_NPASTERISK);
	case KEY_KPPLUS:     return set_input_msg(self, msg, SPR16_KEYCODE_NPPLUS);
	case KEY_KPMINUS:    return set_input_msg(self, msg, SPR16_KEYCODE_NPMINUS);
	case KEY_KPDOT:      return set_input_msg(self, msg, SPR16_KEYCODE_NPDOT);
	case KEY_KPENTER:    return set_input_msg(self, msg, SPR16_KEYCODE_NPENTER);


	case KEY_F1:  return set_input_msg(self, msg, SPR16_KEYCODE_F1);
	case KEY_F2:  return set_input_msg(self, msg, SPR16_KEYCODE_F2);
	case KEY_F3:  return set_input_msg(self, msg, SPR16_KEYCODE_F3);
	case KEY_F4:  return set_input_msg(self, msg, SPR16_KEYCODE_F4);
	case KEY_F5:  return set_input_msg(self, msg, SPR16_KEYCODE_F5);
	case KEY_F6:  return set_input_msg(self, msg, SPR16_KEYCODE_F6);
	case KEY_F7:  return set_input_msg(self, msg, SPR16_KEYCODE_F7);
	case KEY_F8:  return set_input_msg(self, msg, SPR16_KEYCODE_F8);
	case KEY_F9:  return set_input_msg(self, msg, SPR16_KEYCODE_F9);
	case KEY_F10: return set_input_msg(self, msg, SPR16_KEYCODE_F10);
	case KEY_F11: return set_input_msg(self, msg, SPR16_KEYCODE_F11);
	case KEY_F12: return set_input_msg(self, msg, SPR16_KEYCODE_F12);
	case KEY_F13: return set_input_msg(self, msg, SPR16_KEYCODE_F13);
	case KEY_F14: return set_input_msg(self, msg, SPR16_KEYCODE_F14);
	case KEY_F15: return set_input_msg(self, msg, SPR16_KEYCODE_F15);
	case KEY_F16: return set_input_msg(self, msg, SPR16_KEYCODE_F16);
	case KEY_F17: return set_input_msg(self, msg, SPR16_KEYCODE_F17);
	case KEY_F18: return set_input_msg(self, msg, SPR16_KEYCODE_F18);
	case KEY_F19: return set_input_msg(self, msg, SPR16_KEYCODE_F19);
	case KEY_F20: return set_input_msg(self, msg, SPR16_KEYCODE_F20);
	case KEY_F21: return set_input_msg(self, msg, SPR16_KEYCODE_F21);
	case KEY_F22: return set_input_msg(self, msg, SPR16_KEYCODE_F22);
	case KEY_F23: return set_input_msg(self, msg, SPR16_KEYCODE_F23);
	case KEY_F24: return set_input_msg(self, msg, SPR16_KEYCODE_F24);

	/*case KEY_KPASTERISK: return set_input_msg(self, msg,  break;*/
	/*case KEY_CAPSLOCK:   return set_input_msg(self, msg, shift ? '' : '');*/

	/* chars <= 127 */
	case KEY_ESC: return set_input_msg(self, msg, 27);  /* ascii escape code */
	case KEY_0:   return set_input_msg(self, msg, shift ? ')' : '0');
	case KEY_1:   return set_input_msg(self, msg, shift ? '!' : '1');
	case KEY_2:   return set_input_msg(self, msg, shift ? '@' : '2');
	case KEY_3:   return set_input_msg(self, msg, shift ? '#' : '3');
	case KEY_4:   return set_input_msg(self, msg, shift ? '$' : '4');
	case KEY_5:   return set_input_msg(self, msg, shift ? '%' : '5');
	case KEY_6:   return set_input_msg(self, msg, shift ? '^' : '6');
	case KEY_7:   return set_input_msg(self, msg, shift ? '&' : '7');
	case KEY_8:   return set_input_msg(self, msg, shift ? '*' : '8');
	case KEY_9:   return set_input_msg(self, msg, shift ? '(' : '9');

	case KEY_MINUS:      return set_input_msg(self, msg, shift ? '_'  : '-' );
	case KEY_EQUAL:      return set_input_msg(self, msg, shift ? '+'  : '=' );
	case KEY_BACKSPACE:  return set_input_msg(self, msg, shift ? '\b' : '\b');
	case KEY_TAB:        return set_input_msg(self, msg, shift ? '\t' : '\t');
	case KEY_LEFTBRACE:  return set_input_msg(self, msg, shift ? '{'  : '[' );
	case KEY_RIGHTBRACE: return set_input_msg(self, msg, shift ? '}'  : ']' );
	case KEY_ENTER:      return set_input_msg(self, msg, shift ? '\n' : '\n');
	case KEY_SEMICOLON:  return set_input_msg(self, msg, shift ? ':'  : ';' );
	case KEY_APOSTROPHE: return set_input_msg(self, msg, shift ? '"'  : '\'');
	case KEY_GRAVE:      return set_input_msg(self, msg, shift ? '~'  : '`' );
	case KEY_BACKSLASH:  return set_input_msg(self, msg, shift ? '|'  : '\\');
	case KEY_COMMA:      return set_input_msg(self, msg, shift ? '<'  : ',' );
	case KEY_DOT:        return set_input_msg(self, msg, shift ? '>'  : '.' );
	case KEY_SLASH:      return set_input_msg(self, msg, shift ? '?'  : '/' );
	case KEY_SPACE:      return set_input_msg(self, msg, shift ? ' '  : ' ' );

	case KEY_A: return set_input_msg(self, msg, shift ? 'A' : 'a');
	case KEY_B: return set_input_msg(self, msg, shift ? 'B' : 'b');
	case KEY_C: return set_input_msg(self, msg, shift ? 'C' : 'c');
	case KEY_D: return set_input_msg(self, msg, shift ? 'D' : 'd');
	case KEY_E: return set_input_msg(self, msg, shift ? 'E' : 'e');
	case KEY_F: return set_input_msg(self, msg, shift ? 'F' : 'f');
	case KEY_G: return set_input_msg(self, msg, shift ? 'G' : 'g');
	case KEY_H: return set_input_msg(self, msg, shift ? 'H' : 'h');
	case KEY_I: return set_input_msg(self, msg, shift ? 'I' : 'i');
	case KEY_J: return set_input_msg(self, msg, shift ? 'J' : 'j');
	case KEY_K: return set_input_msg(self, msg, shift ? 'K' : 'k');
	case KEY_L: return set_input_msg(self, msg, shift ? 'L' : 'l');
	case KEY_M: return set_input_msg(self, msg, shift ? 'M' : 'm');
	case KEY_N: return set_input_msg(self, msg, shift ? 'N' : 'n');
	case KEY_O: return set_input_msg(self, msg, shift ? 'O' : 'o');
	case KEY_P: return set_input_msg(self, msg, shift ? 'P' : 'p');
	case KEY_Q: return set_input_msg(self, msg, shift ? 'Q' : 'q');
	case KEY_R: return set_input_msg(self, msg, shift ? 'R' : 'r');
	case KEY_S: return set_input_msg(self, msg, shift ? 'S' : 's');
	case KEY_T: return set_input_msg(self, msg, shift ? 'T' : 't');
	case KEY_U: return set_input_msg(self, msg, shift ? 'U' : 'u');
	case KEY_V: return set_input_msg(self, msg, shift ? 'V' : 'v');
	case KEY_W: return set_input_msg(self, msg, shift ? 'W' : 'w');
	case KEY_X: return set_input_msg(self, msg, shift ? 'X' : 'x');
	case KEY_Y: return set_input_msg(self, msg, shift ? 'Y' : 'Y');
	case KEY_Z: return set_input_msg(self, msg, shift ? 'Z' : 'z');

	}
	return evdev_translate_btns(self, msg);
}

/*
 *  prevent vt switching keys from being sent to clients
 *  ctrl-alt-tab cycle through screens
 *  raise sigterm on ctrl-alt-escape
 */
int preempt_keycodes(struct input_device *self, struct spr16_msgdata_input *msg)
{
	/* only care about keydown */
	if (msg->val != 1)
		return 0;

	switch (msg->code)
	{
	/* linux vt switch keys if alt is down */
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
		if (btn_is_down(self, SPR16_KEYCODE_LALT)) {
			return 1;
		}
		break;

	case SPR16_KEYCODE_ESCAPE:
		if (btn_is_down(self, SPR16_KEYCODE_LALT)
				&& btn_is_down(self, SPR16_KEYCODE_LCTRL)) {
			if (self->func_hotkey)
				self->func_hotkey(SPR16_HOTKEY_AXE, NULL);
			else
				raise(SIGTERM);
			return 1;
		}
		break;

	case '\t':
		if (btn_is_down(self, SPR16_KEYCODE_LALT)
				&& btn_is_down(self, SPR16_KEYCODE_LCTRL)) {
			if (self->func_hotkey)
				self->func_hotkey(SPR16_HOTKEY_NEXTSCREEN, self->srv_ctx);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

static float apply_curve(float delta, float min_delta, float max_delta, float slope)
{
	float ds, stable_curve;

	/*
	 * acceleration/stabalization curve
	 *
	 *   slope
	 *   ---------------
	 *   0.0   flat               .  .  .  .  .  .  .  .
	 *   3.0   shallow            ,   |
	 *   10.0  steep              ,   |
	 *   100.0 freefall          .    |
	 *                          ,     |
	 *                    .  , '      |
	 *                                v
	 *                              0.1 (10% of surface)
	 *
	 *     0.0  < ----  x=delta slope=10.0 y=newdelta ---- > 1.0
	 */
	ds = delta * slope;
	ds = (ds * ds) + min_delta;
	stable_curve = fmin(max_delta, ds);
	if (delta < 0.0f)
		return stable_curve * -1.0f;
	else
		return stable_curve;

}

static int abs_to_trackpad(struct drv_evdev_pvt *self,
			   struct spr16_msgdata_input *msg,
			   float min_delta,
			   float max_delta)
{
	float fval, delta;
	int code;

	/* calculate delta */
	fval = ((float)msg->val / (float)msg->ext);
	switch (msg->code)
	{
		case ABS_X:
			delta = fval - self->track_x;
			self->track_x = fval;
			code = REL_X;
			break;
		case ABS_Y:
			delta = fval - self->track_y;
			self->track_y = fval;
			code = REL_Y;
			break;
		default:
			return -1;
	}

	/* ignore initial coords while re-establishing tracking position */
	if (self->tap_reacquire > 0) {
		--self->tap_reacquire;
		return -1;
	}

	if (self->relative_accel) {
		delta = apply_curve(delta, min_delta, max_delta,
				self->relative_accel / SPR16_RELATIVE_SCALE);
	}

	if (fabs(delta) > min_delta * 12.5) {
		self->last_bigmotion = self->curtime;
	}
	delta *= CURVE_SCALE * SPR16_RELATIVE_SCALE;
	msg->type = SPR16_INPUT_AXIS_RELATIVE;
	msg->code = code;
	msg->val  = (int)delta;
	return 0;
}

static int check_tap_dist(struct drv_evdev_pvt *self, float tapclick_dist)
{
	float sx = fabs(self->track_x - self->tap_up_x);
	float sy = fabs(self->track_y - self->tap_up_y);
	if (sx <= tapclick_dist && sy <= tapclick_dist)
		return 1;
	return 0;
}

/* convert to 0->max, increases max if min is negative */
static void clamp_abs(int *val, int *ext, int min, int max, int invert)
{
	if (*val < min)
		*val = min;
	if (*val > max)
		*val = max;
	*val -= min;
	*ext = max-min;
	if (invert)
		*val = *ext - *val;
}

static int set_id(int *id_array, int contact, int new_id)
{
	if (contact < 0 || contact >= SPR16_SURFACE_MAX_CONTACTS) {
		return -1;
	}
	id_array[contact] = new_id;
	return new_id;
}

static int get_id(int *id_array, int contact)
{
	if (contact < 0 || contact >= SPR16_SURFACE_MAX_CONTACTS) {
		return -1;
	}
	return id_array[contact];
}

/* returns number of surface events consumed */
static unsigned int consume_surface_report(struct input_device *self, int client,
					   struct input_event *events, unsigned int i,
					   unsigned int count)
{
	struct drv_evdev_pvt *pvt = self->private;
	struct input_event *event;
	struct spr16_msgdata_input_surface data;
	struct spr16_msghdr hdr;
	unsigned int start = i;
	int min;
	int max;
	int active_id = -1;
	int active_contact = pvt->active_contact;
	int send_msg = 0;

	/* TODO this is poorly named, confuses easily with SPR16_INPUT_SURFACE
	 * use SPR16_MSGHDR_ instead?
	 */
	hdr.type = SPRITEMSG_INPUT_SURFACE;
	memset(&data, 0, sizeof(data));

	active_id = get_id(pvt->contact_ids, active_contact);
	for (; i < count; ++i)
	{
	event = &events[i];
	switch (event->code)
	{
	case ABS_MT_SLOT:
		/* send pending msg */
		if (send_msg) {
			/*data.input.code = active_id;*/
			data.input.type = SPR16_INPUT_SURFACE;
			data.input.code = active_contact;
			data.input.val  = pvt->contact_mag[active_contact];
			data.input.ext  = pvt->contact_magmax;
			data.xpos = pvt->contact_x[active_contact];
			data.ypos = pvt->contact_y[active_contact];
			data.xmax = pvt->contact_xmax;
			data.ymax = pvt->contact_ymax;
			spr16_write_msg(client, &hdr, &data, sizeof(data));
			memset(&data, 0, sizeof(data));
			send_msg = 0;
		}

		if (event->value < 0 || event->value >= SPR16_SURFACE_MAX_CONTACTS) {
			active_id = -1;
			active_contact = -1;
			break;
		}
		active_contact = event->value;
		active_id = get_id(pvt->contact_ids, active_contact);
		if (pvt->contact_mag[active_contact] == 0)
			pvt->contact_mag[active_contact] = 1;
		break;

	case ABS_MT_TRACKING_ID:
		if (active_contact < 0) {
			/* protocol b initial contact is -1 ! */
			active_contact = 0;
		}

		if (event->value < 0) {
			/* active contact released */
			/*data.input.code = active_id;*/
			pvt->contact_mag[active_contact] = 0;
			data.input.type = SPR16_INPUT_SURFACE;
			data.input.code = active_contact;
			data.input.val = pvt->contact_mag[active_contact];
			data.input.ext = pvt->contact_magmax;
			data.xpos = pvt->contact_x[active_contact];
			data.ypos = pvt->contact_y[active_contact];
			data.xmax = pvt->contact_xmax;
			data.ymax = pvt->contact_ymax;
			spr16_write_msg(client, &hdr, &data, sizeof(data));
			memset(&data, 0, sizeof(data));
			send_msg = 0;
			active_id = set_id(pvt->contact_ids, active_contact, -1);
			active_contact = -1;
			continue;
		}
#if 0
		if (get_id(pvt->contact_ids, active_contact) != -1) {
			/* for protocolA support moving over to contact id
			 * will reveal bug here (on bcm5974) when you spam
			 * >= 3 touches sometimes one touch up gets lost,
			 */
		}
#endif
		active_id = set_id(pvt->contact_ids, active_contact, event->value);
		send_msg = 1;
		break;

	case ABS_MT_POSITION_X:
		if (active_id < 0)
			continue;
		pvt->contact_x[active_contact] = event->value;
		min = pvt->absinfo[ABS_MT_POSITION_X].minimum;
		max = pvt->absinfo[ABS_MT_POSITION_X].maximum;
		clamp_abs(&pvt->contact_x[active_contact],
				&pvt->contact_xmax,
				min, max, pvt->invert_x);
		send_msg = 1;
		break;

	case ABS_MT_POSITION_Y:
		if (active_id < 0)
			continue;
		pvt->contact_y[active_contact] = event->value;
		min = pvt->absinfo[ABS_MT_POSITION_Y].minimum;
		max = pvt->absinfo[ABS_MT_POSITION_Y].maximum;
		clamp_abs(&pvt->contact_y[active_contact],
				&pvt->contact_ymax,min, max, pvt->invert_y);
		send_msg = 1;
		break;

	case ABS_MT_TOUCH_MAJOR:
		if (active_id < 0)
			continue;
		if (!pvt->has_pressure) {
			/* compute magnitude off minor axis */
			pvt->contact_mag[active_contact] = event->value;
			min = 1;
			max = 1+(pvt->absinfo[ABS_MT_TOUCH_MAJOR].maximum/3);
			clamp_abs(&pvt->contact_mag[active_contact],
					&pvt->contact_magmax, min, max, 0);
			send_msg = 1;
		}

	case ABS_MT_PRESSURE:
		if (active_id < 0)
			continue;
		if (!pvt->has_pressure)
			break;
		/* XXX untested */
		pvt->contact_mag[active_contact] = event->value;
		min = pvt->absinfo[ABS_MT_PRESSURE].minimum;
		max = pvt->absinfo[ABS_MT_PRESSURE].maximum;
		clamp_abs(&pvt->contact_mag[active_contact],
				&pvt->contact_magmax, min, max, 0);
		send_msg = 1;
		break;
	case SYN_MT_REPORT:
		printf("TODO - consumed %d mt proto A events\n", i-start);
		printf("protocolA is currently not supported\n");
		/* TODO protocol A contacts ? */
		/* use active_id instead of active_contact  there is a
		 * slight bug where up event gets lost when spamming 3
		 * finger touches, so a new id will come through in a used slot.
		 * this is why i'm sending slot number instead of tracking id
		 * right now, i don't have any other multitouch hardware :S
		 */
		break;

	case ABS_MT_DISTANCE:
		break;
	default:
		/* return when non multi-touch event is found */
		if (event->code < ABS_MT_SLOT || event->code > ABS_MT_TOOL_Y) {
			if (send_msg && active_id >= 0) {
				/*data.input.code = active_id;*/
				data.input.type = SPR16_INPUT_SURFACE;
				data.input.code = active_contact;
				data.input.val = pvt->contact_mag[active_contact];
				data.input.ext = pvt->contact_magmax;
				data.xpos = pvt->contact_x[active_contact];
				data.ypos = pvt->contact_y[active_contact];
				data.xmax = pvt->contact_xmax;
				data.ymax = pvt->contact_ymax;
				spr16_write_msg(client, &hdr, &data, sizeof(data));
				memset(&data, 0, sizeof(data));
				send_msg = 0;
			}
			if (i)
				--i; /* rewind event */
			goto ret_out;
		}
		break;
	}
	}
ret_out:
	pvt->active_contact = active_contact;
	return (i-start);
}


/* TODO SYN_DROPPED!! also hardware repeats are getting ignored by Xorg
 * also, we probably want to EVIOCGRAB/REVOKE
 * */
#define EV_BUF ((EV_MAX+1)*2)
int transceive_evdev(int fd, int event_flags, void *user_data)
{
	struct input_device *self = user_data;
	int cl_fd;
	/* TODO gui+config file settings for these */
	const float min_delta = 0.000075f; /* % of surface */
	const float max_delta = 1.0f;
	struct drv_evdev_pvt *pvt = self->private;
	struct input_event events[EV_BUF];
	struct spr16_msgdata_input data;
	struct spr16_msghdr hdr;
	unsigned int i, count;
	int r;


	cl_fd = get_focused_client(self->srv_ctx);
	update_state(self->srv_ctx);

	/* TODO for correctness loop until EAGAIN */
	(void)event_flags;
	clock_gettime(CLOCK_MONOTONIC_RAW, &pvt->curtime);
interrupted:
	r = read(fd, events, sizeof(events));
	if (r == -1) {
		if (errno == EAGAIN) {
			return FDPOLL_HANDLER_OK;
		}
		else if (errno == EINTR) {
			goto interrupted;
		}
		return FDPOLL_HANDLER_REMOVE;
	}

	if (!spr16_server_is_active()) {
		return FDPOLL_HANDLER_OK;
	}

	if (r < (int)sizeof(struct input_event)
			|| r % (int)sizeof(struct input_event)) {
		return FDPOLL_HANDLER_REMOVE;
	}
	count = r / sizeof(struct input_event);

	for (i = 0; i < count; ++i)
	{
		struct input_event *event = &events[i];
		memset(&data, 0, sizeof(data));
		/* if we get SYN_DROPPED we will have to enter a state where
		 * we discard everything until the next SYN?? and maybe call
		 * some ioctl to sync states before moving on, but i don't know
		 * how to test that right now. do we just stop reading?  TODO
		 */
		hdr.type = SPRITEMSG_INPUT;
		data.id = self->device_id;
		switch (event->type)
		{
		case EV_KEY:
			data.code = event->code;
			data.type = SPR16_INPUT_KEY;
			data.val  = event->value;
			if (evdev_translate_keycode(self, &data))
				continue;
			if (preempt_keycodes(self, &data))
				continue;
			break;

		case EV_REL:
			data.type = SPR16_INPUT_AXIS_RELATIVE;
			data.code = event->code;
			data.val  = event->value;

			/* scroll wheel */
			if (pvt->vscroll_amount && data.code == REL_WHEEL) {
				if (data.val > 0)
					data.val = pvt->vscroll_amount;
				else
					data.val = -pvt->vscroll_amount;
				break; /* no accel if amount is specified */
			}

			/* not scroll wheel */
			if (pvt->relative_accel) {
				float drel = data.val / CURVE_SCALE;
				drel = apply_curve(drel, 0.0001f, 0.15f,
						  pvt->relative_accel
						/ SPR16_RELATIVE_SCALE);
				data.val = (int)(drel*SPR16_RELATIVE_SCALE*CURVE_SCALE);
			}
			else {
				data.val = data.val * SPR16_RELATIVE_SCALE;
			}
			break;

		case EV_ABS:
			data.type = SPR16_INPUT_AXIS_ABSOLUTE;
			data.code = event->code;
			data.val  = event->value;
			/* multi-touch surface */
			if (data.code >= ABS_MT_SLOT && data.code <= ABS_MT_TOOL_Y) {
				i+= consume_surface_report(self,cl_fd,events,i,count);
				continue;
			}
			else if (data.code >= ABS_CNT) {
				continue;
			}
			clamp_abs(&data.val, &data.ext,
					pvt->absinfo[data.code].minimum,
					pvt->absinfo[data.code].maximum, 0);

			if (pvt->is_trackpad
					&& (data.code == ABS_X || data.code == ABS_Y)) {
				/* convert to relative */
				if (abs_to_trackpad(pvt, &data, min_delta, max_delta)) {
					continue;
				}
			}
			break;
		case EV_SYN:
			if (event->code == SYN_DROPPED) {
				printf("SYN DROPPED\n");
			}
			continue;
		default:
			continue;
		}

		if (cl_fd == -1) {
			return FDPOLL_HANDLER_OK; /* TODO this is prolly wrong,
						     should continue to check
				     	for SYN_DROPPED and change state if needed.
				     	how to test that?? */
		}
		if (spr16_write_msg(cl_fd, &hdr, &data, sizeof(data))) {
			/* FIXME eagain will end up dropping events,
			 *
			 * message functions could be handled a little better,
			 * like a peer object that can buffer events to handle eagain.
			 * and send all messages once instead of multiple syscalls.
			 */
			/*return (errno == EAGAIN) ? 0 : -1;*/
			return FDPOLL_HANDLER_OK;
		}
	}

	/* send trackpad tap click event */
	if (pvt->is_trackpad && pvt->tap_check) {
		pvt->tap_check = 0;
		pvt->tap_reacquire = 0;
		if (check_tap_dist(pvt, 0.167f)) {
			memset(&data, 0, sizeof(data));
			hdr.type = SPRITEMSG_INPUT;
			data.type = SPR16_INPUT_KEY;
			data.id   = self->device_id;
			data.code = SPR16_KEYCODE_CONTACT;
			data.val  = 1;
			if (spr16_write_msg(cl_fd, &hdr, &data, sizeof(data))) {
				/* FIXME too  ^^ */
				/*return (errno == EAGAIN) ? 0 : -1;*/
				return FDPOLL_HANDLER_OK;
			}
		}
	}
	return FDPOLL_HANDLER_OK;
}

/*
 * may not always get you the right device, use env var to specify exact device
 * export EVDEV_KEYBOARD=event1
 * export EVDEV_MOUSE=event0
 * export EVDEV_TOUCH=event0
 */
struct prospective_path {
	char path[128];
	unsigned int key, rel, abs, led, ff;
	unsigned int surface;
} g_prospect;
static void select_prp(struct prospective_path *p, char *path, unsigned int key,
		unsigned int rel, unsigned int abs, unsigned int ff)
{
	snprintf(p->path, sizeof(p->path), "%s", path);
	p->key = key;
	p->rel = rel;
	p->abs = abs;
	p->ff  = ff;
	printf("prospective input device: %s\n", path);
	printf("key(%d) rel(%d) abs(%d) ff(%d)\n", key, rel, abs, ff);
}

static struct prospective_path *check_environ(int dev_class)
{
	char *e = NULL;
	const unsigned int maxlen = 9; /* event0000 */
	unsigned int i;

	memset(&g_prospect, 0, sizeof(g_prospect));
	switch (dev_class)
	{
	case SPR16_DEV_KEYBOARD:
		e = getenv("EVDEV_KEYBOARD");
		break;
	case SPR16_DEV_MOUSE:
		e = getenv("EVDEV_MOUSE");
		break;
	case SPR16_DEV_TOUCH:
		e = getenv("EVDEV_TOUCH");
		break;
	default:
		return NULL;
	}
	if (e == NULL)
		return NULL;

	/* TODO let user say they don't want to auto detect specific classes */
	if (strncmp(e, "event", 5) != 0) {
		printf("EVDEV env var should be the device name, e.g: event1\n");
		return NULL;
	}

	/* expects 0-9 digits */
	for (i = 0; i < maxlen; ++i)
	{
		if (e[i] == '\0') {
			if (i == 5) {
				printf("missing evdev device number\n");
				return NULL;
			}
			break;
		}
		if (i >= 5 && (e[i] < '0' || e[i] > '9')) {
			printf("erroneous evdev device number\n");
			return NULL;
		}
	}
	if (i >= maxlen)
		return NULL;

	snprintf(g_prospect.path, sizeof(g_prospect.path), "%s/%s", devdir, e);
	return &g_prospect;
}

/* potentially, but not probably */
static int possibly_keyboard(unsigned long *keybits)
{
	int ctrl = bit_check(keybits, KEY_LEFTCTRL, KEY_CNT)
			|| bit_check(keybits, KEY_RIGHTCTRL, KEY_CNT);
	int alt = bit_check(keybits, KEY_LEFTALT,  KEY_CNT)
			|| bit_check(keybits, KEY_RIGHTALT, KEY_CNT);

	if (!ctrl || !alt
			|| !bit_check(keybits, KEY_SPACE, KEY_CNT)
			|| !bit_check(keybits, KEY_BACKSPACE, KEY_CNT)
			|| !bit_check(keybits, KEY_ENTER, KEY_CNT)
			|| !bit_check(keybits, KEY_TAB, KEY_CNT)
			|| !bit_check(keybits, KEY_ESC, KEY_CNT)) {
		return 0;
	}
	else {
		return 1;
	}
}

static int probably_surface(unsigned long *absbits, unsigned long *keybits) {
	int btn = bit_check(keybits, BTN_TOUCH, KEY_CNT);
	int abs =  (bit_check(absbits, ABS_X, ABS_CNT)
		 && bit_check(absbits, ABS_Y, ABS_CNT))
		|| (bit_check(absbits, ABS_MT_POSITION_X, ABS_CNT)
		 && bit_check(absbits, ABS_MT_POSITION_Y, ABS_CNT));
	printf("btn %d abs %d\n", btn, abs);
	if (!btn || !abs) {
		return 0;
	}
	else {
		return 1;
	}
}
static struct prospective_path *find_most_capable(int dev_class)
{
	unsigned long evbits[NLONGS(EV_CNT)];
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
	memset(evbits,  0, sizeof(evbits));
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
		if (ioctl(devfd, EVIOCGBIT(0, sizeof(evbits)), evbits) == -1)
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
		switch (dev_class)
		{
		case SPR16_DEV_KEYBOARD:

			if (!possibly_keyboard(keybits)) {
				break;
			}
			if (led > g_prospect.led) {
				select_prp(&g_prospect, path, key, rel, abs, ff);
			}
			if (key > g_prospect.key) {
				select_prp(&g_prospect, path, key, rel, abs, ff);
			}
			else if (key == g_prospect.key) {
				if (rel > g_prospect.rel) {
					select_prp(&g_prospect, path, key, rel, abs, ff);
				}
				else if (abs > g_prospect.abs) {
					select_prp(&g_prospect, path, key, rel, abs, ff);
				}
				else if (ff > g_prospect.ff) {
					select_prp(&g_prospect, path, key, rel, abs, ff);
				}
			}
			break;
		case SPR16_DEV_MOUSE:
			if (g_prospect.key == 0 && rel >= 2)
				select_prp(&g_prospect, path, key, rel, abs, ff);
			else if (rel >= 2 && rel > g_prospect.rel)
				select_prp(&g_prospect, path, key, rel, abs, ff);
			break;
		case SPR16_DEV_TOUCH:
			/* uses the first one found */
			if (g_prospect.surface)
				break;
			if (probably_surface(absbits, keybits)) {
				select_prp(&g_prospect, path, key, rel, abs, ff);
				g_prospect.surface = 1;
			}
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
	printf("find capable error: %s\n", STRERR);
	close(devfd);
	closedir(dir);
	return NULL;
}

static int add_device(struct input_device *dev, struct input_device **device_list)
{
	static uint8_t g_new_id = 0;
	if (g_new_id >= UINT8_MAX) {
		printf("too many devices\n");
		return -1;
	}
	dev->next = *device_list;
	dev->device_id = ++g_new_id;
	*device_list = dev;
	return 0;
}


/* TODO  config file as well as environ (environ overrides) */
void evdev_load_settings(struct drv_evdev_pvt *pvt, unsigned int dev_class)
{
	if (dev_class == SPR16_DEV_MOUSE || dev_class == SPR16_DEV_TOUCH) {
		pvt->relative_accel = g_srv_opts.pointer_accel;
		pvt->vscroll_amount = g_srv_opts.vscroll_amount;
	}
}

/*
 * TODO, check for duplicates, EVIOCGRAB or use owner/group
 */
int evdev_instantiate(struct server_context *ctx,
		      char *devpath,
		      unsigned int dev_class)
{

	struct fdpoll_handler *fdpoll = ctx->fdpoll;
	struct input_device **device_list = &ctx->input_devices;
	struct input_device *dev = NULL;
	struct drv_evdev_pvt *pvt = NULL;
	char devname[64];
	int devfd;
	int i;

	devfd = open(devpath, O_RDONLY);
	if (devfd == -1) {
		printf("open(%s): %s\n", devpath, STRERR);
		return -1;
	}
	if (ioctl(devfd, EVIOCGNAME(sizeof(devname)-1), devname) == -1) {
		printf("evdev get name: %s\n", STRERR);
		return -1;
	}

	dev = calloc(1, sizeof(struct input_device));
	if (dev == NULL)
		return -1;

	pvt = calloc(1, sizeof(struct drv_evdev_pvt));
	if (pvt == NULL) {
		free(dev);
		return -1;
	}

	pvt->touch_lbtn = -1;
	pvt->touch_rbtn = -1;
	pvt->touch_mbtn = -1;
	pvt->active_contact = -1;
	pvt->invert_x = 0;
	pvt->invert_y = 0;
	pvt->contact_magmax = 1;
	for (i = 0; i < SPR16_SURFACE_MAX_CONTACTS; ++i)
	{
		pvt->contact_ids[i] = -1;
	}
	if (ioctl(devfd, EVIOCGBIT(0, sizeof(pvt->evbits)), pvt->evbits) == -1)
		goto err_free;
	if (ioctl(devfd, EVIOCGBIT(EV_KEY, sizeof(pvt->keybits)), pvt->keybits) == -1)
		goto err_free;
	if (ioctl(devfd, EVIOCGBIT(EV_REL, sizeof(pvt->relbits)), pvt->relbits) == -1)
		goto err_free;
	if (ioctl(devfd, EVIOCGBIT(EV_ABS, sizeof(pvt->absbits)), pvt->absbits) == -1)
		goto err_free;
	if (ioctl(devfd, EVIOCGBIT(EV_LED, sizeof(pvt->ledbits)), pvt->ledbits) == -1)
		goto err_free;
	if (ioctl(devfd, EVIOCGBIT(EV_FF,  sizeof(pvt->ffbits)),  pvt->ffbits) == -1)
		goto err_free;

	/*if (bit_check(pvt->evbits, EV_KEY, EV_CNT)) {
		for (i = 0; i < KEY_CNT; ++i) {
			if (bit_check(pvt->keybits, i, KEY_CNT)) {
				printf("KEY/BTN %02x\n", i);
			}
		}
	}*/

	if (bit_check(pvt->evbits, EV_ABS, EV_CNT)) {
		for (i = 0; i < ABS_CNT; ++i) {
			if (bit_check(pvt->absbits, i, ABS_CNT)) {
				if (ioctl(devfd, EVIOCGABS(i), &pvt->absinfo[i]) == -1) {
					printf("evdev get abs(%d): %s\n", i, STRERR);
					goto err_free;
				}
				/*printf("---------- ABS %02x -------------\n", i);
				printf("val  %d\n", pvt->absinfo[i].value);
				printf("min  %d\n", pvt->absinfo[i].minimum);
				printf("max  %d\n", pvt->absinfo[i].maximum);
				printf("fuzz %d\n", pvt->absinfo[i].fuzz);
				printf("flat %d\n", pvt->absinfo[i].flat);
				printf("res  %d\n", pvt->absinfo[i].resolution);*/
			}
		}
		/* figure out what buttons to use TODO env var override,
		 * hrmmm actually longterm TODO, could emulate pointer entirely
		 * on keyboard as a fallback option.
		 */
		/* touch_lbtn */
		if (bit_check(pvt->keybits, BTN_LEFT, KEY_CNT))
			pvt->touch_lbtn = BTN_LEFT;
		else if (bit_check(pvt->keybits, BTN_TOOL_MOUSE, KEY_CNT))
			pvt->touch_lbtn = BTN_TOOL_MOUSE;
		/*else if (bit_check(pvt->keybits, BTN_TOUCH, KEY_CNT))
			pvt->touch_lbtn = BTN_TOUCH;
		else if (bit_check(pvt->keybits, BTN_TOOL_FINGER, KEY_CNT))
			pvt->touch_lbtn = BTN_TOOL_FINGER; */
		else
			pvt->touch_lbtn = -1;
		/* touch_rbtn */
		if (bit_check(pvt->keybits, BTN_RIGHT, KEY_CNT))
			pvt->touch_rbtn = BTN_RIGHT;
		else if (bit_check(pvt->keybits, BTN_STYLUS2, KEY_CNT))
			pvt->touch_rbtn = BTN_STYLUS2;
		else if (bit_check(pvt->keybits, BTN_TOOL_AIRBRUSH, KEY_CNT))
			pvt->touch_rbtn = BTN_TOOL_AIRBRUSH;
		else if (bit_check(pvt->keybits, BTN_TOOL_TRIPLETAP, KEY_CNT))
			pvt->touch_rbtn = BTN_TOOL_TRIPLETAP;
		else
			pvt->touch_rbtn = -1;

		/* touch_mbtn */
		if (bit_check(pvt->keybits, BTN_MIDDLE, KEY_CNT))
			pvt->touch_mbtn = BTN_MIDDLE;
		else if (bit_check(pvt->keybits, BTN_TOOL_QUADTAP, KEY_CNT))
			pvt->touch_mbtn = BTN_TOOL_QUADTAP;
		else if (bit_check(pvt->keybits, BTN_TOOL_QUINTTAP, KEY_CNT))
			pvt->touch_mbtn = BTN_TOOL_QUINTTAP;
		else if (bit_check(pvt->keybits, BTN_TOOL_RUBBER, KEY_CNT))
			pvt->touch_mbtn = BTN_TOOL_RUBBER;
		else
			pvt->touch_mbtn = -1;

		/* scroll btn */
		if (bit_check(pvt->keybits, BTN_TOOL_DOUBLETAP, KEY_CNT))
			pvt->touch_sbtn = BTN_TOOL_DOUBLETAP;
		else
			pvt->touch_sbtn = -1;

		/* 2d input surface */
		if (bit_check(pvt->absbits, ABS_MT_SLOT, ABS_CNT)
				&&(bit_check(pvt->absbits, ABS_MT_POSITION_X, ABS_CNT)
				|| bit_check(pvt->absbits, ABS_MT_POSITION_Y, ABS_CNT))){
			if (!bit_check(pvt->absbits, ABS_MT_TRACKING_ID, ABS_CNT)) {
				printf("missing mt tracking id capability?\n");
				goto err_free;
			}
			pvt->is_mt_surface = 1;
			if (bit_check(pvt->absbits, ABS_MT_PRESSURE, ABS_CNT)) {
				pvt->has_pressure = 1;
				pvt->contact_magmax =
					pvt->absinfo[ABS_MT_PRESSURE].maximum;
			}
			else if (bit_check(pvt->absbits, ABS_MT_TOUCH_MAJOR, ABS_CNT)) {
				pvt->contact_magmax =
					pvt->absinfo[ABS_MT_TOUCH_MAJOR].maximum;
			}
		}
	}
#if 0
	/* ignore regular ABS if multitouch surface*/
	if (pvt->is_mt_surface) {
		unsigned long absbits[NLONGS(ABS_CNT)];
		struct input_mask msk;
		memset(absbits, 0, sizeof(absbits));
		memset(&msk, 0, sizeof(struct input_mask));
		msk.type = EV_ABS;
		msk.codes_ptr = ptr_to_krn(absbits);
		msk.codes_size = sizeof(absbits);
		if (ioctl(devfd, EVIOCGMASK, &msk) == 0) {
			/* this must be set because of how consume_surface_report
			 * checks for SYN_REPORT which == ABS_X, FIXME when real
			 * efficient even buffering is in place. ABS_MT really should
			 * have been made it's own evdev type :| */
			/*bit_clear(absbits, ABS_X);
			bit_clear(absbits, ABS_Y);*/
			bit_clear(absbits, ABS_WHEEL);
			if (ioctl(devfd, EVIOCSMASK, &msk) == 0) {
				printf("EVIOCSMASK failed: %s\n", STRERR);
			}
		}
		else {
			printf("EVIOCGMASK failed: %s\n", STRERR);
		}
	}
#endif

	/* load user specified settings */
	evdev_load_settings(pvt, dev_class);

	clock_gettime(CLOCK_MONOTONIC_RAW, &pvt->curtime);
	pvt->last_tap_up = pvt->curtime;
	pvt->last_bigmotion = pvt->curtime;
	snprintf(dev->name, sizeof(dev->name), "%s", devname);
	snprintf(dev->path, sizeof(dev->path), "%s", devpath);
	dev->fd = devfd;
	dev->func_flush = generic_flush;
	dev->private = pvt;
	dev->srv_ctx = ctx;

	printf("adding evdev device(%s)\n", devpath);
	if (fdpoll_handler_add(fdpoll, devfd, FDPOLLIN, transceive_evdev, dev)) {
		printf("fdpoll_handler_add(%d) failed, evdev input\n", devfd);
		goto err_free;
	}

	if (add_device(dev, device_list)) {
		fdpoll_handler_remove(fdpoll, devfd);
		goto err_free;
	}

	return 0;

err_free:
	free(pvt);
	free(dev);
	return -1;
}

/* TODO setup drv struct for callback, change name to instantiate for consistency */
static int load_stream(struct server_context *ctx, int streamfd, int mode)
{

	struct fdpoll_handler *fdpoll = ctx->fdpoll;
	struct input_device **device_list = &ctx->input_devices;
	struct input_device *dev;

	/* set to nonblocking */
	/*int fl;
	fl = fcntl(streamfd, F_GETFL);
	if (fl == -1) {
		printf("fcntl(GETFL): %s\n", STRERR);
		return -1;
	}
	if (fcntl(streamfd, F_SETFL, fl | O_NONBLOCK) == -1) {
		printf("fcntl(SETFL, O_NONBLOCK): %s\n", STRERR);
		return -1;
	}*/
	/*if (tcsetpgrp(streamfd, getpgid(0)) == -1) {
		printf("tcsetpgrp: %s\n", STRERR);
		return -1;
	}*/

	dev = calloc(1, sizeof(struct input_device));
	if (dev == NULL)
		return -1;
	dev->fd = streamfd;
	dev->srv_ctx = ctx;
	if (mode == 1) {
		snprintf(dev->name, sizeof(dev->name), "ascii-stream");
		dev->func_flush = generic_flush;
		if (fdpoll_handler_add(fdpoll, streamfd, FDPOLLIN,
					transceive_ascii, dev)) {
			printf("fdpoll_handler_add(%d) failed, ascii input\n", streamfd);
			return -1;
		}
	}
	else {
		snprintf(dev->name, sizeof(dev->name), "raw-stream");
		dev->func_flush = generic_flush;
		if (fdpoll_handler_add(fdpoll, streamfd, FDPOLLIN,
					transceive_raw, dev)) {
			printf("fdpoll_handler_add(%d) failed, raw input\n", streamfd);
			return -1;;
		}
	}

	if (add_device(dev, device_list)) {
		free(dev);
		fdpoll_handler_remove(fdpoll, streamfd);
		return -1;
	}
	return 0;
}

void load_linux_input_drivers(struct server_context *ctx,
			      int stdin_mode,
			      int evdev,
			      input_hotkey hk)
{
	struct input_device **device_list = &ctx->input_devices;

	/* ascii */
	if (stdin_mode == 1) {
		load_stream(ctx, STDIN_FILENO, 1);
	} /* raw */
	else if (stdin_mode == 2) {
		load_stream(ctx, STDIN_FILENO, 2);
	}

	if (evdev) {
		struct prospective_path *prp;

		/* TODO, fix ascii/raw, don't use evdev kbd if they are specified */
		prp = check_environ(SPR16_DEV_KEYBOARD);
		if (prp == NULL) {
			prp = find_most_capable(SPR16_DEV_KEYBOARD);
		}
		if (prp) {
			if (evdev_instantiate(ctx, prp->path, SPR16_DEV_KEYBOARD)) {
				printf("evdev instantiate kbd failed %s\n", prp->path);
			}
			else {
				(*device_list)->func_hotkey = hk;
				printf("using keyboard: %s\n", prp->path);
			}
		}

		prp = check_environ(SPR16_DEV_MOUSE);
		if (prp == NULL) {
			prp = find_most_capable(SPR16_DEV_MOUSE);
		}
		if (prp) {
			if (evdev_instantiate(ctx, prp->path, SPR16_DEV_MOUSE)) {
				printf("evdev instantiate mouse failed %s\n", prp->path);
			}
			else {
				printf("using mouse: %s\n", prp->path);
			}
		}

		prp = check_environ(SPR16_DEV_TOUCH);
		if (prp == NULL) {
			prp = find_most_capable(SPR16_DEV_TOUCH);
		}
		if (prp) {
			if (evdev_instantiate(ctx, prp->path, SPR16_DEV_TOUCH)) {
				printf("evdev instantiate touch failed %s\n", prp->path);
			}
			else {
				if (getenv("SPR16_TRACKPAD")) {
					struct drv_evdev_pvt *drv;
					drv = (*device_list)->private;
					drv->is_trackpad = 1;
					if (getenv("SPR16_POINTER_ACCEL") == NULL) {
						drv->relative_accel = 100;
					}
					printf("using trackpad device: %s\n", prp->path);
				}
				else {
					printf("using touch device: %s\n", prp->path);
				}
			}
		}
	}
}

