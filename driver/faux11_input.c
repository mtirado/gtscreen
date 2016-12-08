/*
 * Copyright 1999 by Frederic Lepied, France. <Lepied@XFree86.org>
 *                                                                            
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is  hereby granted without fee, provided that
 * the  above copyright   notice appear  in   all  copies and  that both  that
 * copyright  notice   and   this  permission   notice  appear  in  supporting
 * documentation, and that   the  name of  Frederic   Lepied not  be  used  in
 * advertising or publicity pertaining to distribution of the software without
 * specific,  written      prior  permission.     Frederic  Lepied   makes  no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.                   
 *                                                                            
 * FREDERIC  LEPIED DISCLAIMS ALL   WARRANTIES WITH REGARD  TO  THIS SOFTWARE,
 * INCLUDING ALL IMPLIED   WARRANTIES OF MERCHANTABILITY  AND   FITNESS, IN NO
 * EVENT  SHALL FREDERIC  LEPIED BE   LIABLE   FOR ANY  SPECIAL, INDIRECT   OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA  OR PROFITS, WHETHER  IN  AN ACTION OF  CONTRACT,  NEGLIGENCE OR OTHER
 * TORTIOUS  ACTION, ARISING    OUT OF OR   IN  CONNECTION  WITH THE USE    OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * ---------------------------------------------------------------------------
 *  originally void.c from xf86-input-void
 *  modified: 2016 Michael R. Tirado
 */

#define _GNU_SOURCE
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <misc.h>
#include <xf86.h>
#define NEED_XF86_TYPES 1
#if !defined(DGUX)
#include <xisb.h>
#endif
#include <xf86_OSproc.h>
#include <xf86Xinput.h>
#include <exevents.h>		/* Needed for InitValuator/Proximity stuff */
#include <X11/keysym.h>
#include <mipointer.h>

#include <xf86Module.h>

#include <X11/Xatom.h>
#include <xorg-server.h>
#include <xserver-properties.h>

#define MAXBUTTONS 3

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
#error "XINPUT ABI 12 required."
#endif

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include "faux11.h"
#include <xorg/input.h>
#include <xorg/xkbsrv.h>
#define STRERR strerror(errno)


/* how to generalize valuators... */
ValuatorMask *g_cursor;

/******************************************************************************
 * Function/Macro keys variables
 *****************************************************************************/

static void BellProc(int percent, DeviceIntPtr pDev, pointer ctrl, int unused) {}
static void KeyControlProc(DeviceIntPtr pDev, KeybdCtrl *ctrl) {}
static void PointerControlProc(DeviceIntPtr dev, PtrCtrl *ctrl) {}

static uint32_t ctrl_to_x11(unsigned char c)
{
	switch (c)
	{
		case  8: return XK_BackSpace;
		case  9: return XK_Tab;
		case 10: return XK_Return;
		case 13: return XK_Return;
		case 14: return XK_Shift_L;
		case 15: return XK_Shift_L; /* TODO ascii shift up */
		case 27: return XK_Escape;
		default: return 0;
	}
}

static uint32_t to_x11(uint16_t k_c)
{
	if (k_c <= 0x00ff) /* ascii */ {
		return k_c;
	}

	switch (k_c)
	{
		case SPR16_KEYCODE_CAPSLOCK:	return XK_Caps_Lock;
		case SPR16_KEYCODE_LSHIFT:	return XK_Shift_L;
		case SPR16_KEYCODE_RSHIFT:	return XK_Shift_R;
		case SPR16_KEYCODE_LCTRL:	return XK_Control_L;
		case SPR16_KEYCODE_RCTRL:	return XK_Control_R;
		case SPR16_KEYCODE_LALT:	return XK_Alt_L;
		case SPR16_KEYCODE_RALT:	return XK_Alt_R;
		case SPR16_KEYCODE_UP:		return XK_Up;
		case SPR16_KEYCODE_DOWN:	return XK_Down;
		case SPR16_KEYCODE_LEFT:	return XK_Left;
		case SPR16_KEYCODE_RIGHT:   	return XK_Right;
		case SPR16_KEYCODE_PAGEUP:	return XK_Page_Up;
		case SPR16_KEYCODE_PAGEDOWN:	return XK_Page_Down;
		case SPR16_KEYCODE_HOME:	return XK_Home;
		case SPR16_KEYCODE_END:		return XK_End;
		case SPR16_KEYCODE_INSERT:	return XK_Insert;
		case SPR16_KEYCODE_DELETE:	return XK_Delete;
		case SPR16_KEYCODE_F1:		return XK_F1;
		case SPR16_KEYCODE_F2:		return XK_F2;
		case SPR16_KEYCODE_F3:		return XK_F3;
		case SPR16_KEYCODE_F4:		return XK_F4;
		case SPR16_KEYCODE_F5:		return XK_F5;
		case SPR16_KEYCODE_F6:		return XK_F6;
		case SPR16_KEYCODE_F7:		return XK_F7;
		case SPR16_KEYCODE_F8:		return XK_F8;
		case SPR16_KEYCODE_F9:		return XK_F9;
		case SPR16_KEYCODE_F10:		return XK_F10;
		case SPR16_KEYCODE_F11:		return XK_F11;
		case SPR16_KEYCODE_F12:		return XK_F12;
		case SPR16_KEYCODE_F13:		return XK_F13;
		case SPR16_KEYCODE_F14:		return XK_F14;
		case SPR16_KEYCODE_F15:		return XK_F15;
		case SPR16_KEYCODE_F16:		return XK_F16;
		case SPR16_KEYCODE_F17:		return XK_F17;
		case SPR16_KEYCODE_F18:		return XK_F18;
		case SPR16_KEYCODE_F19:		return XK_F19;
		case SPR16_KEYCODE_F20:		return XK_F20;
		case SPR16_KEYCODE_F21:		return XK_F21;
		case SPR16_KEYCODE_F22:		return XK_F22;
		case SPR16_KEYCODE_F23:		return XK_F23;
		case SPR16_KEYCODE_F24:		return XK_F24;
		default: return 0;
	}
}


/* this is a bit much (malloc every key press), i'm not sure if KeySym's
 * value/min/max are expected to change, if theres an event for that, and
 * or if i can just cache them here indefinitely. TODO figure out.
 * if the size never changes we can at least eliminate malloc from this mess.
 */
static uint32_t spr16_to_x11(DeviceIntPtr dev, struct spr16_msgdata_input *msg)
{
	unsigned int i, w;
	KeySymsPtr symbols;
	KeySym key;
	uint32_t ret = 0;

	symbols = XkbGetCoreMap(dev);
	if (symbols == NULL) {
		fprintf(stderr, "XkbGetCoreMap is null\n");
		return 0;
	}

	if (msg->code < ' ') {
		key = ctrl_to_x11(msg->code);
	}
	else {
		key = to_x11(msg->code);
	}

	/* ascii keycodes map directly */
	for (i = symbols->minKeyCode; i < symbols->maxKeyCode; ++i)
	{
		for (w = 0; w < symbols->mapWidth; ++w)
		{
			KeySym ksm = symbols->map[( (i - symbols->minKeyCode)
							* symbols->mapWidth) + w];
			if (ksm == key) {
				ret = i;
				goto do_ret;
			}
			/*else if (ksm == 0) {
				break;
			}*/
		}
	}
do_ret:
	free(symbols->map);
	free(symbols);
	return ret;
}

static int ascii_kbd_is_shifted(unsigned char c)
{
	return (	(c > 32  && c < 44 && c != 39)
		|| 	(c > 57  && c < 91 && c != 61 && c != 59)
		|| 	(c > 93  && c < 96)
		|| 	(c > 122 && c < 127));
}

static void fx11_ascii_mode(InputInfoPtr info, struct spr16_msgdata_input *msg)
{
	uint32_t k_c;
	int state_shifted;

	state_shifted = ascii_kbd_is_shifted(msg->code);
	k_c = spr16_to_x11(info->dev, msg);

	/* hack for ascii mode to be usable without proper shift down/up */
	if (state_shifted) {
		struct spr16_msgdata_input fake_shift;
		uint32_t shift_key;
		fake_shift.bits = 0;
		fake_shift.code = 14;
		shift_key = spr16_to_x11(info->dev, &fake_shift);
		xf86PostKeyboardEvent(info->dev, shift_key, 1);
		xf86PostKeyboardEvent(info->dev, k_c, 1);
		xf86PostKeyboardEvent(info->dev, k_c, 0);
		xf86PostKeyboardEvent(info->dev, shift_key, 0);
	}
	else {
		xf86PostKeyboardEvent(info->dev, k_c, 1);
		xf86PostKeyboardEvent(info->dev, k_c, 0);
	}
}

static void reset_modifiers(InputInfoPtr info)
{
	struct spr16_msgdata_input in;
	uint32_t k_c;
	memset(&in, 0, sizeof(in));
	/* this is a bit of a hack, i'm sorry. vt switching causes key up event
	 * without initial key down, these artificial key ups seem to help out. */
	in.code = SPR16_KEYCODE_LSHIFT;
	k_c = spr16_to_x11(info->dev, &in);
	xf86PostKeyboardEvent(info->dev, k_c, 0);
	in.code = SPR16_KEYCODE_LALT;
	k_c = spr16_to_x11(info->dev, &in);
	xf86PostKeyboardEvent(info->dev, k_c, 0);
	in.code = SPR16_KEYCODE_LCTRL;
	k_c = spr16_to_x11(info->dev, &in);
	xf86PostKeyboardEvent(info->dev, k_c, 0);
}

static void axis_relative(struct spr16_msgdata_input *msg)
{
	valuator_mask_zero(g_cursor);
	if (msg->code == REL_X) {
		valuator_mask_set(g_cursor, REL_X, msg->val);
	}
	else if (msg->code == REL_Y) {
		valuator_mask_set(g_cursor, REL_Y, msg->val);
	}
	/* TODO scroll wheel */
}

static void key_event(InputInfoPtr info, struct spr16_msgdata_input *msg)
{
	uint32_t k_c;

	/* button range is BTN_0 ... BTN_GEAR_UP, currently only deals with mouse */
	if (msg->code >= SPR16_KEYCODE_LBTN && msg->code <= SPR16_KEYCODE_FBTN) {
		switch (msg->code)
		{
			case SPR16_KEYCODE_LBTN:
				k_c = 1;
				break;
			case SPR16_KEYCODE_RBTN:
				k_c = 2;
				break;
			case SPR16_KEYCODE_ABTN:
				k_c = 3;
				break;
				/* from evdev driver:
				 *  BTN_SIDE ... BTN_JOYSTICK  =  8 + code - BTN_SIDE
				 *  BTN_0 ... BTN_2 = 1 + code - BTN_0
				 *  BTN_3 ... BTN_MOUSE - 1 = 8 + code - BTN_3
				 *
				 */
			default:
				return;
		}
		xf86PostButtonEvent(info->dev, Relative, k_c, msg->val == 1, 0, 0);
		return;
	}

	k_c = spr16_to_x11(info->dev, msg);
	if (msg->val == 0) {
		xf86PostKeyboardEvent(info->dev, k_c, 0);
	}
	else if (msg->val == 1) {
		xf86PostKeyboardEvent(info->dev, k_c, 1);
	}
	else if (msg->val == 2) {
		xf86PostKeyboardEvent(info->dev, k_c, 0);
		xf86PostKeyboardEvent(info->dev, k_c, 1);
		/* repeat */
	}
}

static void fx11ReadInput(InputInfoPtr pInfo)
{
	struct spr16_msgdata_input msgs[1024];
	int bytes;
	unsigned int i;
intr:
	errno = 0;
	bytes = read(pInfo->fd, msgs, sizeof(msgs));
	if (bytes < sizeof(struct spr16_msgdata_input)
			|| bytes % sizeof(struct spr16_msgdata_input)) {
		if (errno == EINTR)
			goto intr;
		return;
	}

	for (i = 0; i < bytes / sizeof(struct spr16_msgdata_input); ++i)
	{
		switch (msgs[i].type)
		{
		case SPR16_INPUT_AXIS_RELATIVE:
			axis_relative(&msgs[i]);
			/* should we accumulate and wait for syn? */
			xf86PostMotionEventM(pInfo->dev, Relative, g_cursor);
			break;
		case SPR16_INPUT_NOTICE:
			if (msgs[i].code == SPR16_NOTICE_INPUT_FLUSH)
				reset_modifiers(pInfo); /* vt change */
			break;
		case SPR16_INPUT_KEY:
			key_event(pInfo, &msgs[i]);
			break;
		case SPR16_INPUT_KEY_ASCII:
			/* this is a fallback */
			fx11_ascii_mode(pInfo, &msgs[i]);
			break;
		default:
			break;
		}
	}
	return;
}

/*
 * xf86VoidControlProc --
 *
 * called to change the state of a device.
 */
static int xf86VoidControlProc(DeviceIntPtr device, int what)
{
	InputInfoPtr pInfo;
	unsigned char map[MAXBUTTONS + 1];
	unsigned char i;
	Bool result;
	Atom btn_labels[MAXBUTTONS] = {0};
	Atom axes_labels[2] = {0};

	axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_X);
	axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_Y);

	btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
	btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
	btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);

	pInfo = device->public.devicePrivate;

	switch (what)
	{
		case DEVICE_INIT:
			device->public.on = FALSE;

			for (i = 0; i < MAXBUTTONS; i++) {
				map[i + 1] = i + 1;
			}

			if (InitButtonClassDeviceStruct(device,
						MAXBUTTONS,
						btn_labels,
						map) == FALSE) {
				ErrorF("unable to allocate Button class device\n");
				return -1;
			}

			result = InitKeyboardDeviceStruct(device, NULL,
					BellProc, KeyControlProc);
			if (!result) {
				ErrorF("unable to init keyboard device\n");
				return -1;
			}

			if (InitValuatorClassDeviceStruct(device,
						2,
						axes_labels,
						0,
						Absolute) == FALSE) {
				InitValuatorAxisStruct(device,
						0,
						axes_labels[0],
						0, /* min val */1, /* max val */
						1, /* resolution */
						0, /* min_res */
						1, /* max_res */
						Absolute);
				InitValuatorAxisStruct(device,
						1,
						axes_labels[1],
						0, /* min val */1, /* max val */
						1, /* resolution */
						0, /* min_res */
						1, /* max_res */
						Absolute);
				ErrorF("unable to allocate Valuator class device\n");
				return -1;
			}
			else {
				/* allocate the motion history buffer if needed */
				xf86MotionHistoryAllocate(pInfo);
			}
			if (InitPtrFeedbackClassDeviceStruct(device,
						PointerControlProc) == FALSE) {
				ErrorF("unable to init pointer feedback class device\n");
				return -1;
			}
			break;

		case DEVICE_ON:
			xf86AddEnabledDevice(pInfo);
			device->public.on = TRUE;
			break;

		case DEVICE_OFF:
		case DEVICE_CLOSE:
			if (device->public.on == TRUE) {
				xf86RemoveEnabledDevice(pInfo);
				device->public.on = FALSE;
			}
			break;

		default:
			return BadValue;
	}
	return 0;
}

/*
 * xf86VoidUninit --
 *
 * called when the driver is unloaded.
 */
static void xf86VoidUninit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
	xf86VoidControlProc(pInfo->dev, DEVICE_OFF);
}

/*
 * xf86VoidInit --
 *
 * called when the module subsection is found in XF86Config
 */
static int xf86VoidInit(InputDriverPtr drv, InputInfoPtr pInfo,	int flags)
{
	int ipc[2];
	int pipeflags;

	fprintf(stderr, "--------------------------------------------------\n");
	fprintf(stderr, "- faux11 input init ------------------------------\n");
	fprintf(stderr, "--------------------------------------------------\n");

	if (pipe2(ipc, O_CLOEXEC|O_NONBLOCK|O_DIRECT)) {
		fprintf(stderr, "pipe: %s\n", STRERR);
		return -1;
	}
	/* set async to generate SIGIO (for read_input to trigger) */
	pipeflags = fcntl(ipc[0], F_GETFL, 0);
	if (pipeflags == -1) {
		fprintf(stderr, "fcntl: %s\n", STRERR);
		return -1;
	}
	if (fcntl(ipc[0], F_SETFL, pipeflags|O_ASYNC)) {
		fprintf(stderr, "fcntl: %s\n", STRERR);
		return -1;
	}
	pipeflags = fcntl(ipc[1], F_GETFL, 0);
	if (pipeflags == -1) {
		fprintf(stderr, "fcntl: %s\n", STRERR);
		return -1;
	}
	if (fcntl(ipc[1], F_SETFL, pipeflags|O_ASYNC)) {
		fprintf(stderr, "fcntl: %s\n", STRERR);
		return -1;
	}

	/* Initialise the InputInfoRec. */
	pInfo->type_name = "faux11input";
	pInfo->device_control = xf86VoidControlProc;
	pInfo->read_input = fx11ReadInput;
	pInfo->control_proc = NULL;
	pInfo->switch_mode = NULL;
	pInfo->fd = ipc[0]; /* read end */
	pInfo->private = (void *)ipc[1]; /* write end for gfx driver, such hacks! */

	/* cursor */
	g_cursor = valuator_mask_new(CURSOR_AXES);
	if (g_cursor == NULL) {
		fprintf(stderr, "valuator_mask_new: %s\n", STRERR);
		return -1;
	}
	valuator_mask_zero(g_cursor);
	return 0;
}

_X_EXPORT InputDriverRec FAUX11INPUT = {
	1,			/* driver version */
	"faux11input",		/* driver name */
	NULL,			/* identify */
	xf86VoidInit,		/* pre-init */
	xf86VoidUninit,		/* un-init */
	NULL,			/* module */
};

/*
 ***************************************************************************
 *
 * Dynamic loading functions
 *
 ***************************************************************************
 */
/*
 * xf86VoidUnplug --
 *
 * called when the module subsection is found in XF86Config
 */
static void xf86VoidUnplug(pointer p)
{
}

/*
 * xf86VoidPlug --
 *
 * called when the module subsection is found in XF86Config
 */
static pointer xf86VoidPlug(pointer module, pointer options, int *errmaj, int *errmin)
{
	xf86AddInputDriver(&FAUX11INPUT, module, 0);
	return module;
}

static XF86ModuleVersionInfo xf86VoidVersionRec =
{
	"faux11input",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_XINPUT,
	ABI_XINPUT_VERSION,
	MOD_CLASS_XINPUT,
	{0, 0, 0, 0}		/* signature, to be patched into the file by */
	/* a tool */
};

_X_EXPORT XF86ModuleData faux11inputModuleData = {
	&xf86VoidVersionRec,
	xf86VoidPlug,
	xf86VoidUnplug
};



