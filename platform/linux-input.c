/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
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
#include <sys/epoll.h>
#include <sys/types.h>
#include <time.h>
#include <linux/input.h>
#include <dirent.h>
#include <signal.h>
#include <termios.h>

#include "../protocol/spr16.h"
#include "../defines.h"
#define STRERR strerror(errno)

/* number of longs needed to represent n bits. */
#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(n) ((n+LONG_BITS-1)/sizeof(long))

#define CURVE_SCALE   1000.0f

const char devpfx[] = "event";
const char devdir[] = "/dev/input";

#define TAP_DELAY  210000 /* microseconds between last tap up, for tap click */
#define TAP_STABL  300000 /* delay permitted from last big motion */

extern struct server g_server;

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

/* This is a fallback input mode that computes shift state in a hacky manner,
 * and doesnt support many useful keys like ctrl,alt,capslock,etc
 * we could add raw kbd support too, eventually...
 */
static int transceive_ascii(struct input_device *self, int client)
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
			data.id = self->device_id;
			if (spr16_write_msg(client, &hdr, &data, sizeof(data))) {
				return (errno == EAGAIN) ? 0 : -1;
			}
		}
	}
	return 0;
}

static int transceive_raw(struct input_device *self, int client)
{
	/* TODO */
	(void)self;
	(void)client;
	return -1;
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

static int evdev_translate_btns(struct input_device *self, struct spr16_msgdata_input *msg)
{
	struct drv_evdev_pvt *pvt = self->private;
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

	switch (msg->code)
	{
	/*case BTN_MOUSE: == BTN_LEFT*/
	case BTN_LEFT:    msg->code = SPR16_KEYCODE_LBTN; break;
	case BTN_RIGHT:   msg->code = SPR16_KEYCODE_RBTN; break;
	case BTN_MIDDLE:  msg->code = SPR16_KEYCODE_ABTN; break;
	case BTN_BACK:    msg->code = SPR16_KEYCODE_BBTN; break;
	case BTN_FORWARD: msg->code = SPR16_KEYCODE_CBTN; break;
	case BTN_SIDE:    msg->code = SPR16_KEYCODE_DBTN; break;
	case BTN_EXTRA:   msg->code = SPR16_KEYCODE_EBTN; break;
	case BTN_TASK:    msg->code = SPR16_KEYCODE_FBTN; break;

	case BTN_TOUCH:

		if (!pvt->is_trackpad) {
			msg->code = SPR16_KEYCODE_CONTACT;
			break;
		}

		if (msg->val == 0) {
			/* contact up */
			pvt->last_tap_up = pvt->curtime;
			pvt->tap_reacquire = 4;
			pvt->tap_up_x = pvt->track_x;
			pvt->tap_up_y = pvt->track_y;
			msg->code = SPR16_KEYCODE_CONTACT;
			break;
		}
		else if (usecs_elapsed(pvt->curtime, pvt->last_tap_up)<TAP_DELAY) {
			if (usecs_elapsed(pvt->curtime, pvt->last_bigmotion)<TAP_STABL) {
				return -1;
			}
			/* send contact down later, after receiving updated positions */
			pvt->tap_check = 1;
		}
		return -1;

	default:
		return -1;
	}
	return 0;
}

static int evdev_translate_keycode(struct input_device *self,
				   struct spr16_msgdata_input *msg)
{
	uint32_t shift = 0;
	/* TODO still missing some important keys */
	switch (msg->code)
	{
	case KEY_LEFTALT:
		msg->code = SPR16_KEYCODE_LALT;
		if (msg->val == 0) {
			if (self->keyflags & SPR16_KEYMOD_LALT)
				self->keyflags &= ~SPR16_KEYMOD_LALT;
		}
		else if (msg->val == 1) {
			self->keyflags |= SPR16_KEYMOD_LALT;
		}
		break;

	case KEY_LEFTCTRL:
		msg->code = SPR16_KEYCODE_LCTRL;
		if (msg->val == 0) {
			if (self->keyflags & SPR16_KEYMOD_LCTRL)
				self->keyflags &= ~SPR16_KEYMOD_LCTRL;
		}
		else if (msg->val == 1) {
			self->keyflags |= SPR16_KEYMOD_LCTRL;
		}
		break;

	case KEY_CAPSLOCK:   msg->code = SPR16_KEYCODE_CAPSLOCK; break;
	case KEY_LEFTSHIFT:  msg->code = SPR16_KEYCODE_LSHIFT;   break;
	case KEY_RIGHTSHIFT: msg->code = SPR16_KEYCODE_RSHIFT;   break;
	case KEY_RIGHTCTRL:  msg->code = SPR16_KEYCODE_RCTRL;    break;
	case KEY_RIGHTALT:   msg->code = SPR16_KEYCODE_RALT;     break;
	case KEY_UP: 	     msg->code = SPR16_KEYCODE_UP;       break;
	case KEY_DOWN:       msg->code = SPR16_KEYCODE_DOWN;     break;
	case KEY_LEFT:       msg->code = SPR16_KEYCODE_LEFT;     break;
	case KEY_RIGHT:      msg->code = SPR16_KEYCODE_RIGHT;    break;
	case KEY_PAGEUP:     msg->code = SPR16_KEYCODE_PAGEUP;   break;
	case KEY_PAGEDOWN:   msg->code = SPR16_KEYCODE_PAGEDOWN; break;
	case KEY_HOME:       msg->code = SPR16_KEYCODE_HOME;     break;
	case KEY_END:        msg->code = SPR16_KEYCODE_END;      break;
	case KEY_INSERT:     msg->code = SPR16_KEYCODE_INSERT;   break;
	case KEY_DELETE:     msg->code = SPR16_KEYCODE_DELETE;   break;

	case KEY_NUMLOCK:     msg->code = SPR16_KEYCODE_NUMLOCK;    break;
	case KEY_KP0:         msg->code = SPR16_KEYCODE_NP0;        break;
	case KEY_KP1:         msg->code = SPR16_KEYCODE_NP1;        break;
	case KEY_KP2:         msg->code = SPR16_KEYCODE_NP2;        break;
	case KEY_KP3:         msg->code = SPR16_KEYCODE_NP3;        break;
	case KEY_KP4:         msg->code = SPR16_KEYCODE_NP4;        break;
	case KEY_KP5:         msg->code = SPR16_KEYCODE_NP5;        break;
	case KEY_KP6:         msg->code = SPR16_KEYCODE_NP6;        break;
	case KEY_KP7:         msg->code = SPR16_KEYCODE_NP7;        break;
	case KEY_KP8:         msg->code = SPR16_KEYCODE_NP8;        break;
	case KEY_KP9:         msg->code = SPR16_KEYCODE_NP9;        break;
	case KEY_KPSLASH:     msg->code = SPR16_KEYCODE_NPSLASH;    break;
	case KEY_KPASTERISK:  msg->code = SPR16_KEYCODE_NPASTERISK; break;
	case KEY_KPPLUS:      msg->code = SPR16_KEYCODE_NPPLUS;     break;
	case KEY_KPMINUS:     msg->code = SPR16_KEYCODE_NPMINUS;    break;
	case KEY_KPDOT:       msg->code = SPR16_KEYCODE_NPDOT;      break;
	case KEY_KPENTER:     msg->code = SPR16_KEYCODE_NPENTER;    break;


	case KEY_F1:  msg->code = SPR16_KEYCODE_F1;  break;
	case KEY_F2:  msg->code = SPR16_KEYCODE_F2;  break;
	case KEY_F3:  msg->code = SPR16_KEYCODE_F3;  break;
	case KEY_F4:  msg->code = SPR16_KEYCODE_F4;  break;
	case KEY_F5:  msg->code = SPR16_KEYCODE_F5;  break;
	case KEY_F6:  msg->code = SPR16_KEYCODE_F6;  break;
	case KEY_F7:  msg->code = SPR16_KEYCODE_F7;  break;
	case KEY_F8:  msg->code = SPR16_KEYCODE_F8;  break;
	case KEY_F9:  msg->code = SPR16_KEYCODE_F9;  break;
	case KEY_F10: msg->code = SPR16_KEYCODE_F10; break;
	case KEY_F11: msg->code = SPR16_KEYCODE_F11; break;
	case KEY_F12: msg->code = SPR16_KEYCODE_F12; break;
	case KEY_F13: msg->code = SPR16_KEYCODE_F13; break;
	case KEY_F14: msg->code = SPR16_KEYCODE_F14; break;
	case KEY_F15: msg->code = SPR16_KEYCODE_F15; break;
	case KEY_F16: msg->code = SPR16_KEYCODE_F16; break;
	case KEY_F17: msg->code = SPR16_KEYCODE_F17; break;
	case KEY_F18: msg->code = SPR16_KEYCODE_F18; break;
	case KEY_F19: msg->code = SPR16_KEYCODE_F19; break;
	case KEY_F20: msg->code = SPR16_KEYCODE_F20; break;
	case KEY_F21: msg->code = SPR16_KEYCODE_F21; break;
	case KEY_F22: msg->code = SPR16_KEYCODE_F22; break;
	case KEY_F23: msg->code = SPR16_KEYCODE_F23; break;
	case KEY_F24: msg->code = SPR16_KEYCODE_F24; break;

	/*case KEY_KPASTERISK: msg->code =  break;*/
	/*case KEY_CAPSLOCK:   msg->code = shift ? '' : ''; break;*/

	/* chars <= 127 */
	case KEY_ESC: msg->code = 27; break;  /* ascii escape code */
	case KEY_0: msg->code = shift ? ')' : '0'; break;
	case KEY_1: msg->code = shift ? '!' : '1'; break;
	case KEY_2: msg->code = shift ? '@' : '2'; break;
	case KEY_3: msg->code = shift ? '#' : '3'; break;
	case KEY_4: msg->code = shift ? '$' : '4'; break;
	case KEY_5: msg->code = shift ? '%' : '5'; break;
	case KEY_6: msg->code = shift ? '^' : '6'; break;
	case KEY_7: msg->code = shift ? '&' : '7'; break;
		case KEY_8: msg->code = shift ? '*' : '8'; break;
	case KEY_9: msg->code = shift ? '(' : '9'; break;

	case KEY_MINUS:      msg->code = shift ? '_'  : '-' ; break;
	case KEY_EQUAL:      msg->code = shift ? '+'  : '=' ; break;
	case KEY_BACKSPACE:  msg->code = shift ? '\b' : '\b'; break;
	case KEY_TAB:        msg->code = shift ? '\t' : '\t'; break;
	case KEY_LEFTBRACE:  msg->code = shift ? '{'  : '[' ; break;
	case KEY_RIGHTBRACE: msg->code = shift ? '}'  : ']' ; break;
	case KEY_ENTER:      msg->code = shift ? '\n' : '\n'; break;
	case KEY_SEMICOLON:  msg->code = shift ? ':'  : ';' ; break;
	case KEY_APOSTROPHE: msg->code = shift ? '"'  : '\''; break;
	case KEY_GRAVE:      msg->code = shift ? '~'  : '`' ; break;
	case KEY_BACKSLASH:  msg->code = shift ? '|'  : '\\'; break;
	case KEY_COMMA:      msg->code = shift ? '<'  : ',' ; break;
	case KEY_DOT:        msg->code = shift ? '>'  : '.' ; break;
	case KEY_SLASH:      msg->code = shift ? '?'  : '/' ; break;
	case KEY_SPACE:      msg->code = shift ? ' '  : ' ' ; break;

	case KEY_A: msg->code = shift ? 'A' : 'a'; break;
	case KEY_B: msg->code = shift ? 'B' : 'b'; break;
	case KEY_C: msg->code = shift ? 'C' : 'c'; break;
	case KEY_D: msg->code = shift ? 'D' : 'd'; break;
	case KEY_E: msg->code = shift ? 'E' : 'e'; break;
	case KEY_F: msg->code = shift ? 'F' : 'f'; break;
	case KEY_G: msg->code = shift ? 'G' : 'g'; break;
	case KEY_H: msg->code = shift ? 'H' : 'h'; break;
	case KEY_I: msg->code = shift ? 'I' : 'i'; break;
	case KEY_J: msg->code = shift ? 'J' : 'j'; break;
	case KEY_K: msg->code = shift ? 'K' : 'k'; break;
	case KEY_L: msg->code = shift ? 'L' : 'l'; break;
	case KEY_M: msg->code = shift ? 'M' : 'm'; break;
	case KEY_N: msg->code = shift ? 'N' : 'n'; break;
	case KEY_O: msg->code = shift ? 'O' : 'o'; break;
	case KEY_P: msg->code = shift ? 'P' : 'p'; break;
	case KEY_Q: msg->code = shift ? 'Q' : 'q'; break;
	case KEY_R: msg->code = shift ? 'R' : 'r'; break;
	case KEY_S: msg->code = shift ? 'S' : 's'; break;
	case KEY_T: msg->code = shift ? 'T' : 't'; break;
	case KEY_U: msg->code = shift ? 'U' : 'u'; break;
	case KEY_V: msg->code = shift ? 'V' : 'v'; break;
	case KEY_W: msg->code = shift ? 'W' : 'w'; break;
	case KEY_X: msg->code = shift ? 'X' : 'x'; break;
	case KEY_Y: msg->code = shift ? 'Y' : 'Y'; break;
	case KEY_Z: msg->code = shift ? 'Z' : 'z'; break;

	default: return evdev_translate_btns(self, msg);
	}
	return 0;
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
	case '\t':
		if ((self->keyflags & (SPR16_KEYMOD_LALT | SPR16_KEYMOD_LCTRL))
				   == (SPR16_KEYMOD_LALT | SPR16_KEYMOD_LCTRL)) {
			if (self->func_hotkey)
				self->func_hotkey(SPR16_HOTKEY_NEXTSCREEN, NULL);
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
		 * slight bug where up event doesnt gets lost when spamming 3
		 * finger touches, so a new id will come through in a used slot.
		 * this is why i'm sending slot number instead of tracking id
		 * right now, i don't have any other multitouch hardware.
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
 * also, we probably want to EVIOCGRAB, is that essentially locking the device?
 * */
#define EV_BUF ((EV_MAX+1)*2)
static int transceive_evdev(struct input_device *self, int client)
{
	/* TODO gui+config file settings for these */
	const float min_delta = 0.000075f; /* % of surface */
	const float max_delta = 1.0f;
	struct drv_evdev_pvt *pvt = self->private;
	struct input_event events[EV_BUF];
	struct spr16_msgdata_input data;
	struct spr16_msghdr hdr;
	unsigned int i, count;
	int r;

	clock_gettime(CLOCK_MONOTONIC_RAW, &pvt->curtime);
interrupted:
	r = read(self->fd, events, sizeof(events));
	if (r == -1) {
		if (errno == EAGAIN) {
			return 0;
		}
		else if (errno == EINTR) {
			goto interrupted;
		}
		fprintf(stderr, "read input: %s\n", STRERR);
		return -1;
	}
	if (r < (int)sizeof(struct input_event)
			|| r % (int)sizeof(struct input_event)) {
		return -1;
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
				i+= consume_surface_report(self,client,events,i,count);
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

		if (client == -1)
			return 0; /* TODO this is prolly wrong, should continue to check
				     for SYN_DROPPED and change state if needed.
				     how to test that?? */
		if (spr16_write_msg(client, &hdr, &data, sizeof(data))) {
			/* FIXME eagain will end up dropping events,
			 *
			 * message functions could be handled a little better,
			 * like a peer object that can buffer events to handle eagain.
			 * and send all messages once instead of multiple syscalls.
			 */
			return (errno == EAGAIN) ? 0 : -1;
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
			if (spr16_write_msg(client, &hdr, &data, sizeof(data))) {
				/* FIXME too  ^^ */
				return (errno == EAGAIN) ? 0 : -1;
			}
		}
	}
	return 0;
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
/*static void bit_set(unsigned long *arr, uint32_t bit)
{
	arr[bit / LONG_BITS] |= (1LU << (bit % LONG_BITS));
}*/
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
		pvt->relative_accel = g_server.pointer_accel;
		pvt->vscroll_amount = g_server.vscroll_amount;
	}
}

/*
 * TODO, check for duplicates, EVIOCGRAB or use owner/group
 */
int evdev_instantiate(struct input_device **device_list,
				int epoll_fd, char *devpath, unsigned int dev_class)
{
	struct epoll_event ev;
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
		else if (bit_check(pvt->keybits, BTN_TOUCH, KEY_CNT))
			pvt->touch_lbtn = BTN_TOUCH;
		else if (bit_check(pvt->keybits, BTN_TOOL_FINGER, KEY_CNT))
			pvt->touch_lbtn = BTN_TOOL_FINGER;
		else
			pvt->touch_lbtn = -1;
		/* touch_rbtn */
		if (bit_check(pvt->keybits, BTN_RIGHT, KEY_CNT))
			pvt->touch_rbtn = BTN_RIGHT;
		else if (bit_check(pvt->keybits, BTN_STYLUS2, KEY_CNT))
			pvt->touch_rbtn = BTN_STYLUS2;
		else if (bit_check(pvt->keybits, BTN_TOOL_AIRBRUSH, KEY_CNT))
			pvt->touch_rbtn = BTN_TOOL_AIRBRUSH;
		else if (bit_check(pvt->keybits, BTN_TOOL_DOUBLETAP, KEY_CNT))
			pvt->touch_rbtn = BTN_TOOL_DOUBLETAP;
		else
			pvt->touch_rbtn = -1;

		/* touch_mbtn */
		if (bit_check(pvt->keybits, BTN_MIDDLE, KEY_CNT))
			pvt->touch_mbtn = BTN_MIDDLE;
		else if (bit_check(pvt->keybits, BTN_TOOL_QUADTAP, KEY_CNT))
			pvt->touch_mbtn = BTN_TOOL_QUADTAP;
		else if (bit_check(pvt->keybits, BTN_TOOL_QUINTTAP, KEY_CNT))
			pvt->touch_mbtn = BTN_TOOL_QUINTTAP;
		else if (bit_check(pvt->keybits, BTN_TOOL_TRIPLETAP, KEY_CNT))
			pvt->touch_mbtn = BTN_TOOL_TRIPLETAP;
		else if (bit_check(pvt->keybits, BTN_TOOL_RUBBER, KEY_CNT))
			pvt->touch_mbtn = BTN_TOOL_RUBBER;
		else
			pvt->touch_mbtn = -1;

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
(void)bit_clear;
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
	dev->func_transceive = transceive_evdev;
	dev->func_flush = generic_flush;
	dev->private = pvt;
	if (add_device(dev, device_list))
		goto err_free;
	return 0;
err_free:
	free(pvt);
	free(dev);
	return -1;
}

static int load_stream(struct input_device **device_list, int epoll_fd,
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
		return -1;
	}
	if (fcntl(streamfd, F_SETFL, fl | O_NONBLOCK) == -1) {
		printf("fcntl(SETFL, O_NONBLOCK): %s\n", STRERR);
		return -1;
	}

	ev.events = EPOLLIN;
	ev.data.fd = streamfd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, streamfd, &ev)) {
		printf("epoll_ctl(add): %s\n", STRERR);
		return -1;
	}
	/*if (tcsetpgrp(streamfd, getpgid(0)) == -1) {
		printf("tcsetpgrp: %s\n", STRERR);
		return -1;
	}*/
	dev = calloc(1, sizeof(struct input_device));
	if (dev == NULL)
		return -1;
	dev->fd = streamfd;
	if (mode == 1) {
		snprintf(dev->name, sizeof(dev->name), "ascii-stream");
		dev->func_transceive  = transceive_ascii;
		dev->func_flush = generic_flush;
	}
	else {
		snprintf(dev->name, sizeof(dev->name), "raw-stream");
		dev->func_transceive  = transceive_raw;
		dev->func_flush = generic_flush;
	}
	if (add_device(dev, device_list)) {
		free(dev);
		return -1;
	}
	return 0;
}

void load_linux_input_drivers(struct input_device **device_list,
		int epoll_fd, int stdin_mode, int evdev, input_hotkey hk)
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

		prp = check_environ(SPR16_DEV_KEYBOARD);
		if (prp == NULL) {
			prp = find_most_capable(SPR16_DEV_KEYBOARD);
		}
		if (prp) {
			if (evdev_instantiate(device_list, epoll_fd,
						prp->path, SPR16_DEV_KEYBOARD)) {
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
			if (evdev_instantiate(device_list, epoll_fd,
						prp->path, SPR16_DEV_MOUSE)) {
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
			if (evdev_instantiate(device_list, epoll_fd,
						prp->path, SPR16_DEV_TOUCH)) {
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



/* arggh this doesn't work with my USB keyboard :\ */
#if 0
static int evdev_print_keycodes(struct input_device *dev)
{
	unsigned int i;

	printf("----------------------- KEY CODES ----------------------\n");
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





