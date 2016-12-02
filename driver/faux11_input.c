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
#include "faux11.h"
#include <xorg/input.h>
#include <xorg/xkbsrv.h>
#define STRERR strerror(errno)

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
	if (k_c <= 255) /* ascii */
		return k_c;

	switch (k_c)
	{
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
static uint32_t spr16_to_x11(DeviceIntPtr dev, struct spr16_msgdata_input_keyboard msg)
{
	unsigned int i, w;
	KeySymsPtr symbols;
	KeySym key;
	uint32_t ret = 0;

	symbols = XkbGetCoreMap(dev);
	if (symbols == NULL) {
		fprintf(stderr, "XkbGetCoreMap is null\n");
		return -1;
	}

	/*fprintf(stderr, "mapwidth: %d\n", symbols->mapWidth);
	fprintf(stderr, "minkey: %d\n", symbols->minKeyCode);
	fprintf(stderr, "maxkey: %d\n", symbols->maxKeyCode);*/
	if (msg.keycode < ' ') {
		key = ctrl_to_x11(msg.keycode);
	}
	else {
		key = to_x11(msg.keycode);
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

static void fx11_stdin_mode(InputInfoPtr info, struct spr16_msgdata_input_keyboard msg)
{
	uint32_t key_code = msg.keycode;
	uint32_t k_c;
	int state_shifted;

	if (key_code == 27) {
	}
	/* TODO move shift state calculation from server to here,
	 * add raw stdin/ascii where raw sends 1 unmodified byte. */
	state_shifted = msg.flags & SPR16_KBD_SHIFT;
	k_c = spr16_to_x11(info->dev, msg);

	/* hack for ascii mode to be usable without proper shift down/up,
	 * though maybe we can fix this by defining some new control codes
	 * and abandoning traditional concept of special shift/ctrl/alt keys */
	if (state_shifted) {
		struct spr16_msgdata_input_keyboard fake_shift;
		uint32_t shift_key;
		fake_shift.flags = 0;
		fake_shift.keycode = 14;
		shift_key = spr16_to_x11(info->dev, fake_shift);
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

static void fx11ReadInput(InputInfoPtr pInfo)
{
	struct spr16_msgdata_input_keyboard msgs[1024];
	int bytes;
	unsigned int i;
intr:
	errno = 0;
	bytes = read(pInfo->fd, msgs, sizeof(msgs));
	if (bytes < sizeof(struct spr16_msgdata_input_keyboard)
			|| bytes % sizeof(struct spr16_msgdata_input_keyboard)) {
		if (errno == EINTR)
			goto intr;
		return;
	}

	for (i = 0; i < bytes / sizeof(struct spr16_msgdata_input_keyboard); ++i)
	{
		/* for non-evdev users, TODO add raw lnx_kbd support */
		if (msgs[i].flags & SPR16_INPUT_STDIN) {
			fx11_stdin_mode(pInfo, msgs[i]);
		}
		else {
			uint32_t k_c;
			k_c = spr16_to_x11(pInfo->dev, msgs[i]);
			xf86PostKeyboardEvent(pInfo->dev, k_c,
					!(msgs[i].flags & SPR16_INPUT_RELEASE));
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
			fprintf(stderr, "------- DEVICE_ON\n");
			xf86AddEnabledDevice(pInfo);
			device->public.on = TRUE;
			break;

		case DEVICE_OFF:
			fprintf(stderr, "------- DEVICE_OFF\n");
		case DEVICE_CLOSE:
			if (what == DEVICE_CLOSE)
				fprintf(stderr, "----- DEVICE_CLOSE\n");
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

	return 0;
}

_X_EXPORT InputDriverRec FAUX11INPUT = {
	1,				/* driver version */
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


#if 0
static void print_x11_ksyms(DeviceIntPtr dev)
{
	unsigned int i, w;
	KeySymsPtr symbols;

	symbols = XkbGetCoreMap(dev);
	if (symbols == NULL) {
		fprintf(stderr, "null keymap\n");
		return -1;
	}

	for (i = symbols->minKeyCode; i < symbols->maxKeyCode; ++i)
	{
		for (w = 0; w < symbols->mapWidth; ++w)
		{
			KeySym ksm = symbols->map[((i-symbols->minKeyCode)
						* symbols->mapWidth)
						+ w];
				fprintf(stderr, "sym[kc:%d,w:%x] == (%c)0x%x [%d])\n",
						i,w, (char)ksm, ksm, ksm);
		}
	}
	free(symbols->map);
	free(symbols);
}
#endif

