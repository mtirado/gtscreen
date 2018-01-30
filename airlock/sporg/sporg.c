/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 2011 Dave Airlie
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 *
 * Original Author: Alan Hourihane <alanh@tungstengraphics.com>
 * Rewrite: Dave Airlie <airlied@redhat.com>
 *
 * ---------------------------------------------------------------------------
 * Forked: 2016 Michael R. Tirado <mtirado418@gmail.com>
 *
 * This file was originally driver.c from xf86-video-modesetting driver.
 * removed all of the DRM functionality and converted to a spr16 client.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>

#include "xorg-server.h"
#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"
#include "xf86Pci.h"
#include "mipointer.h"
#include "micmap.h"
#include <X11/extensions/randr.h>
#include "fb.h"
#include "edid.h"
#include "xf86i2c.h"
#include "xf86Crtc.h"
#include "xf86cmap.h"
#include "miscstruct.h"
#include "dixstruct.h"
#include "shadow.h"
#include "xf86xv.h"
#include <X11/extensions/Xv.h>

#include "sporg-compat-api.h"
#include "sporg.h"

#define STRERR strerror(errno)

static void AdjustFrame(ADJUST_FRAME_ARGS_DECL);
static Bool CloseScreen(CLOSE_SCREEN_ARGS_DECL);
static Bool EnterVT(VT_FUNC_ARGS_DECL);
static void Identify(int flags);
static const OptionInfoRec *AvailableOptions(int chipid, int busid);
static ModeStatus ValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
			    Bool verbose, int flags);
static void FreeScreen(FREE_SCREEN_ARGS_DECL);
static void LeaveVT(VT_FUNC_ARGS_DECL);
static Bool SwitchMode(SWITCH_MODE_ARGS_DECL);
static Bool ScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool PreInit(ScrnInfoPtr pScrn, int flags);
static Bool Probe(DriverPtr drv, int flags);
static Bool sporg_driver_func(ScrnInfoPtr scrn, xorgDriverFuncOp op, void *data);


struct spr16_msgdata_servinfo g_servinfo;
uint32_t g_maxvram;

enum {
	FAUX_CHIPSET=0
};

_X_EXPORT DriverRec sporg = {
	1,
	"sporg",
	Identify,
	Probe,
	AvailableOptions,
	NULL,
	0,
	sporg_driver_func,
};

static SymTabRec Chipsets[] = {
	{FAUX_CHIPSET, "faux" },
	{-1, NULL}
};

typedef enum
{
	OPTION_SW_CURSOR,
	OPTION_DEVICE_PATH,
} sporgOpts;

static const OptionInfoRec Options[] = {
	{OPTION_SW_CURSOR, "SWcursor", OPTV_BOOLEAN, {0}, FALSE},
	{OPTION_DEVICE_PATH, "fauxdev", OPTV_STRING, {0}, FALSE },
	{-1, NULL, OPTV_NONE, {0}, FALSE}
};

int sporgEntityIndex = -1;

static MODULESETUPPROTO(Setup);

static XF86ModuleVersionInfo VersRec = {
	"sporg",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData sporgModuleData = { &VersRec, Setup, NULL };


static pointer Setup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = 0;

	/* This module should be loaded only once, but check to be sure.
	*/
	if (!setupDone) {
		setupDone = 1;
		g_maxvram = 0;
		xf86AddDriver(&sporg, module, HaveDriverFuncs);

		/*
		 * The return value must be non-NULL on success even though there
		 * is no TearDownProc.
		 */
		return (pointer) 1;
	} else {
		if (errmaj)
			*errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

static void Identify(int flags)
{
	xf86PrintChipsets("sporg", "Fake driver alternative display server",
			Chipsets);
}

static Bool sporg_driver_func(ScrnInfoPtr scrn, xorgDriverFuncOp op, void *data)
{
	xorgHWFlags *flags;
	switch (op)
	{
		case GET_REQUIRED_HW_INTERFACES:
			flags = (CARD32 *)data;
			*flags = HW_SKIP_CONSOLE;
			return TRUE;
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,15,99,902,0)
		case SUPPORTS_SERVER_FDS:
			return TRUE;
#endif
		default:
			return FALSE;
	}
}
static const OptionInfoRec *AvailableOptions(int chipid, int busid)
{
	return Options;
}

static Bool Probe(DriverPtr drv, int flags)
{
	char *socket_name;
	int i, numDevSections;
	GDevPtr *devSections;
	Bool foundScreen = FALSE;
	ScrnInfoPtr scrn = NULL;
	struct spr16_msgdata_servinfo sinfo;
	xf86DrvMsg(0, X_INFO, "-- sporg gfx probe --\n");

	if (g_maxvram)
		return FALSE;

	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;

	/*
	 * Find the config file Device sections that match this
	 * driver, and return if there are none.
	 */
	if ((numDevSections = xf86MatchDevice("sporg", &devSections)) <= 0) {
		return FALSE;
	}

	socket_name = getenv("SPR16_SOCKET");
	if (socket_name == NULL)
		socket_name = SPR16_DEFAULT_SOCKET;
	sinfo = get_servinfo(socket_name);

	g_maxvram  = sinfo.width * sinfo.height * (sinfo.bpp/8) / 1024;
	if (g_maxvram == 0) {
		xf86DrvMsg(0, X_ERROR, "servinfo invalid\n");
		return FALSE;
	}
	g_servinfo = sinfo;

	for (i = 0; i < numDevSections; i++) {

		int entity = xf86ClaimNoSlot(drv,FAUX_CHIPSET,devSections[i],TRUE);
		scrn = xf86AllocateScreen(drv, 0);
		xf86AddEntityToScreen(scrn, entity);
		fprintf(stderr, "scrn: %p entity: %d\n", (void *)scrn, entity);
		if (scrn) {
			/* TODO clean up version / name assignment */
			foundScreen = TRUE;
			scrn->driverVersion = PACKAGE_VERSION_MAJOR;
			scrn->driverName = "sporg";
			scrn->name = "sporg";
			scrn->Probe = Probe;
			scrn->PreInit = PreInit;
			scrn->ScreenInit = ScreenInit;
			scrn->SwitchMode = SwitchMode;
			scrn->AdjustFrame = AdjustFrame;
			scrn->EnterVT = EnterVT;
			scrn->LeaveVT = LeaveVT;
			scrn->FreeScreen = FreeScreen;
			scrn->ValidMode = ValidMode;
			xf86DrvMsg(scrn->scrnIndex, X_INFO, "using faux device");
		}
	}

	free(devSections);

	return foundScreen;
}

static Bool GetRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate)
		return TRUE;

	pScrn->driverPrivate = xnfcalloc(sizeof(sporgRec), 1);

	return TRUE;
}

static void dispatch_dirty(ScreenPtr pScreen)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
	sporgPtr sporg = sporgPTR(scrn);
	RegionPtr dirty = DamageRegion(sporg->damage);
	unsigned num_cliprects = REGION_NUM_RECTS(dirty);
	BoxPtr rect = REGION_RECTS(dirty);

	/* this isn't really vsync'd, more of a rate limit since it's really difficult
	 * to tell clients not to draw during vblank period. it lets the server merge
	 * damage and copy during vblank at least, instead of copying immediately.
	 */
	for (unsigned i = 0; i < num_cliprects; i++, rect++) {
		while(spr16_client_sync(rect->x1,
					rect->y1,
					rect->x2,
					rect->y2,
					SPRITESYNC_FLAG_VBLANK)) {
			if (errno != EAGAIN) {
				fprintf(stderr,"spr16 sync error: %s\n",strerror(errno));
				_exit(-1);
				break;
			}
			else {
				usleep(1000);
			}
		}
	}
	DamageEmpty(sporg->damage);
}

static void sporgBlockHandler(BLOCKHANDLER_ARGS_DECL)
{
	SCREEN_PTR(arg);
	sporgPtr sporg = sporgPTR(xf86ScreenToScrn(pScreen));
	pScreen->BlockHandler = sporg->BlockHandler;
	pScreen->BlockHandler(BLOCKHANDLER_ARGS);
	pScreen->BlockHandler = sporgBlockHandler;
	if (sporg->dirty_enabled)
		dispatch_dirty(pScreen);
}

static void FreeRec(ScrnInfoPtr pScrn)
{
	sporgPtr sporg;

	if (!pScrn)
		return;

	sporg = sporgPTR(pScrn);
	if (!sporg)
		return;
	pScrn->driverPrivate = NULL;

	free(sporg->Options);
	free(sporg);

}


/* device needs to have these parameters set */
static int init_faux_hw(ScrnInfoPtr pScrn)
{
	ClockRangePtr clock_ranges;
	const int min_clock = 1000;
	const int max_clock = 3000000;
	int i;
	GDevPtr device;

	device = xf86GetEntityInfo(pScrn->entityList[0])->device;
	device->videoRam = 32000; /* 32MB TODO how to read this from .conf? */
	/* TODO
	 * set exact device ram instead of wasting space
	 * */

	pScrn->videoRam = device->videoRam;
	pScrn->progClock = TRUE;

	clock_ranges = (ClockRangePtr)xnfcalloc(sizeof(ClockRange), 1);
	clock_ranges->next = NULL;
	clock_ranges->ClockMulFactor = 1;
	clock_ranges->minClock = min_clock;
	clock_ranges->maxClock = max_clock;
	clock_ranges->clockIndex = -1;
	clock_ranges->interlaceAllowed = TRUE;
	clock_ranges->doubleScanAllowed = TRUE;

	i = xf86ValidateModes(pScrn,
				pScrn->monitor->Modes,
				0,
				clock_ranges,
				NULL,
				256,
				g_servinfo.width,
				(16 * pScrn->bitsPerPixel),
				128,
				g_servinfo.height,
				0,
				0,
				pScrn->videoRam * 1024,
				LOOKUP_BEST_REFRESH);
	if (i == -1) {
		xf86DrvMsg(0, X_ERROR, "xf86ValidateModes() fail.\n");
		return -1;
	}
	xf86PruneDriverModes(pScrn);
	if (i == 0 || pScrn->modes == NULL) {
		xf86DrvMsg(0, X_ERROR, "no modes found, use -config to specify modes\n");
		return -1;
	}

	xf86SetCrtcForModes(pScrn, 0);
	/*xf86PrintModes(pScrn);*/
	if (pScrn->modes == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No modes.\n");
		return -1;
	}
	pScrn->currentMode = pScrn->modes;

	/* Set display resolution */
	xf86SetDpi(pScrn, 0, 0);
	return 0;
}

static Bool PreInit(ScrnInfoPtr pScrn, int flags)
{
	sporgPtr sporg;
	EntityInfoPtr pEnt;

	if (pScrn->numEntities != 1)
		return FALSE;
	pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

	if (flags & PROBE_DETECT) {
		return FALSE;
	}

	/* Allocate driverPrivate */
	if (!GetRec(pScrn))
		return FALSE;

	sporg = sporgPTR(pScrn);
	sporg->SaveGeneration = -1;
	sporg->pEnt = pEnt;
	sporg->entityPrivate = NULL;

	pScrn->monitor = pScrn->confScreen->monitor;
	pScrn->progClock = TRUE;
	pScrn->rgbBits = 8;

	if (!xf86SetDepthBpp
			(pScrn, 0, 0, 0, Support24bppFb | Support32bppFb))
		return FALSE;
	switch (pScrn->depth) {
		case 15:
		case 16:
		case 24:
			break;
		default:
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
					"Depth (%d) is not supported by the driver\n",
					pScrn->depth);
			return FALSE;
	}
	xf86PrintDepthBpp(pScrn);

	/* Process the options */
	xf86CollectOptions(pScrn, NULL);
	if (!(sporg->Options = malloc(sizeof(Options))))
		return FALSE;
	memcpy(sporg->Options, Options, sizeof(Options));
	xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, sporg->Options);
	/*if (!xf86SetWeight(pScrn, defaultWeight, defaultWeight))
	  return FALSE;*/
	/* if depth > 8? */
	{
		rgb zeros = { 0.0, 0.0, 0.0 };
		if (!xf86SetWeight(pScrn, zeros, zeros))
			return FALSE;
	}
	/*if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;*/

	/*if (xf86ReturnOptValBool(sporg->Options, OPTION_SW_CURSOR, FALSE)) {
	  sporg->drmmode.sw_cursor = TRUE;
	  }*/

	sporg->cursor_width = 64;
	sporg->cursor_height = 64;

	{
		Gamma zeros = { 0.0, 0.0, 0.0 };

		if (!xf86SetGamma(pScrn, zeros)) {
			return FALSE;
		}
	}

	if (init_faux_hw(pScrn)) {
		xf86DrvMsg(0, X_ERROR, "init_faux_hw fail\n");
		return FALSE;
	}

	/* Load the required sub modules */
	if (!xf86LoadSubModule(pScrn, "fb")) {
		return FALSE;
	}

	pScrn->memPhysBase = 0;
	pScrn->fbOffset = 0;

	return TRUE;
}

static Bool CreateScreenResources(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	sporgPtr sporg = sporgPTR(pScrn);
	PixmapPtr rootPixmap;
	Bool ret;
	void *pixels;
	pScreen->CreateScreenResources = sporg->createScreenResources;
	ret = pScreen->CreateScreenResources(pScreen);
	pScreen->CreateScreenResources = CreateScreenResources;

	pixels = sporg->FBBase;
	if (!pixels)
		return FALSE;

	rootPixmap = pScreen->GetScreenPixmap(pScreen);
	if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
		FatalError("Couldn't adjust screen pixmap\n");

	sporg->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE,
			pScreen, rootPixmap);

	if (sporg->damage) {
		DamageRegister(&rootPixmap->drawable, sporg->damage);
		sporg->dirty_enabled = TRUE;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				"Failed to create screen damage record\n");
		return FALSE;
	}
	return ret;
}

static Bool SaveScreen(ScreenPtr pScreen, int mode)
{
    return TRUE;
}

static int create_input_descriptors(int *ipc)
{
	char buf[32];
	int pipeflags;
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
	if (fcntl(ipc[1], F_SETFL, pipeflags|O_ASYNC)) {
		fprintf(stderr, "fcntl: %s\n", STRERR);
		return -1;
	}
	snprintf(buf, sizeof(buf), "%d", ipc[0]);
	if (setenv("SPORG_INPUT_READ", buf, 1)) {
		fprintf(stderr, "setenv: %s\n", STRERR);
		return -1;
	}
	snprintf(buf, sizeof(buf), "%d", g_servinfo.width);
	if (setenv("SPORG_WIDTH", buf, 1)) {
		fprintf(stderr, "setenv: %s\n", STRERR);
		return -1;
	}
	snprintf(buf, sizeof(buf), "%d", g_servinfo.height);
	if (setenv("SPORG_HEIGHT", buf, 1)) {
		fprintf(stderr, "setenv: %s\n", STRERR);
		return -1;
	}
	return 0;
}

static Bool ScreenInit(SCREEN_INIT_ARGS_DECL)
{
	char *socket_name;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	sporgPtr sporg = sporgPTR(pScrn);
	VisualPtr visual;
	struct spr16 *sprite;
	int ipc[2];

	xf86DrvMsg(0, X_INFO, "---- sporg screen init ---------------------------\n");
	xf86PrintBacktrace();
	xf86DrvMsg(0, X_INFO, "--------------------------------------------------\n");
	pScrn->pScreen = pScreen;

	socket_name = getenv("SPR16_SOCKET");
	if (socket_name == NULL)
		socket_name = SPR16_DEFAULT_SOCKET;

	if (create_input_descriptors(ipc))
		return -1;
	/*pScrn->displayWidth = pScrn->virtualX - 24;*/
	/* TODO check modes against servinfo! then allocate exactly enough "vram"
	 * rename variable form maxvram, maybe allocate a little extra to pad for
	 * simd in which case servinfo needs bytes as well as dimensions
	 * for now, just try to fullscreen it.
	 */
	/* TODO need a function to unmap / free this,
	 * and call it on failures if theres more than one screen. */
	sprite = spr16_connect(socket_name, g_servinfo.width, g_servinfo.height);
	if (!sprite) {
		xf86DrvMsg(0, X_ERROR, "could not connect to spr16 display server\n");
		return FALSE;
	}
	sporg->FBBase = (pointer *)sprite->shmem.addr;
	if (fork_client(ipc[1])) {
		xf86DrvMsg(0, X_ERROR, "unable to start spr16 client\n");
		return FALSE;
	}
	miClearVisualTypes();

	if (!miSetVisualTypes(pScrn->depth,
				miGetDefaultVisualMask(pScrn->depth),
				pScrn->rgbBits, pScrn->defaultVisual))
		return FALSE;

	if (!miSetPixmapDepths())
		return FALSE;

	pScrn->memPhysBase = 0;
	pScrn->fbOffset = 0;

	/* TODO variable size instead of using max */
	if (!fbScreenInit(pScreen, sporg->FBBase,
				g_servinfo.width, g_servinfo.height,
				pScrn->xDpi, pScrn->yDpi,
				pScrn->displayWidth, pScrn->bitsPerPixel))
		return FALSE;

	if (pScrn->bitsPerPixel > 8) {
		/* Fixup RGB ordering */
		visual = pScreen->visuals + pScreen->numVisuals;
		while (--visual >= pScreen->visuals) {
			if ((visual->class | DynamicClass) == DirectColor) {
				visual->offsetRed = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue = pScrn->offset.blue;
				visual->redMask = pScrn->mask.red;
				visual->greenMask = pScrn->mask.green;
				visual->blueMask = pScrn->mask.blue;
			}
		}
	}

	fbPictureInit(pScreen, NULL, 0);

	sporg->createScreenResources = pScreen->CreateScreenResources;
	pScreen->CreateScreenResources = CreateScreenResources;

	xf86SetBlackWhitePixels(pScreen);
	xf86SetBackingStore(pScreen);
	xf86SetSilkenMouse(pScreen);
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	pScreen->SaveScreen = SaveScreen;
	sporg->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = CloseScreen;
	sporg->BlockHandler = pScreen->BlockHandler;
	pScreen->BlockHandler = sporgBlockHandler;

	if (!miCreateDefColormap(pScreen))
		return FALSE;

	xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
	if (EnterVT(VT_FUNC_ARGS) != TRUE)
		return FALSE;
	return TRUE;
}

static void AdjustFrame(ADJUST_FRAME_ARGS_DECL) {}

static void FreeScreen(FREE_SCREEN_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	FreeRec(pScrn);
}

static void LeaveVT(VT_FUNC_ARGS_DECL) {}

/*
 * This gets called when gaining control of the VT, and from ScreenInit().
 */
static Bool EnterVT(VT_FUNC_ARGS_DECL)
{
	return TRUE;
}

static Bool SwitchMode(SWITCH_MODE_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}

static Bool CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	sporgPtr sporg = sporgPTR(pScrn);

	if (sporg->damage) {
#if XORG_VERSION_CURRENT < XORG_VERSION_NUMERIC(1,14,99,2,0)
		DamageUnregister(&pScreen->GetScreenPixmap(pScreen)->drawable,
				 sporg->damage);
#else
		DamageUnregister(sporg->damage);
#endif
		DamageDestroy(sporg->damage);
		sporg->damage = NULL;
	}

	pScreen->CreateScreenResources = sporg->createScreenResources;
	pScreen->BlockHandler = sporg->BlockHandler;
	pScreen->CloseScreen = sporg->CloseScreen;
	return (*pScreen->CloseScreen) (CLOSE_SCREEN_ARGS);
}

static ModeStatus ValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
			    Bool verbose, int flags)
{
	return MODE_OK;
}
