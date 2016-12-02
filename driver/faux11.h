/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 * Author: Alan Hourihane <alanh@tungstengraphics.com>
 * ---------------------------------------------------------------------------
 *
 * Forked: 2016 Michael R. Tirado <mtirado418@gmail.com>
 * This file was originally driver.h from xf86-video-modesetting driver.
 */

#include "faux11-compat-api.h"
#include <errno.h>
#include <damage.h>

#include "../protocol/spr16.h"

/* faux11_client interface */
struct spr16_msgdata_servinfo get_servinfo(char *srv_socket);
struct spr16 *spr16_connect(char *srv_socket, uint16_t width, uint16_t height);
int fork_client();


#define DRV_ERROR(msg)	xf86DrvMsg(pScrn->scrnIndex, X_ERROR, msg);

#define PACKAGE_VERSION_MAJOR		0
#define PACKAGE_VERSION_MINOR		2
#define PACKAGE_VERSION_PATCHLEVEL	0

/* globals */
typedef struct _color
{
    int red;
    int green;
    int blue;
} dummy_colors;


typedef struct
{
    int lastInstance;
    int refCount;
    ScrnInfoPtr pScrn_1;
    ScrnInfoPtr pScrn_2;
} EntRec, *EntPtr;

typedef struct _faux11Rec
{
    int fd;

    EntPtr entityPrivate;

    int Chipset;
    EntityInfoPtr pEnt;

    Bool noAccel;
    CloseScreenProcPtr CloseScreen;

    /* Broken-out options. */
    OptionInfoPtr Options;

    unsigned int SaveGeneration;

    CreateScreenResourcesProcPtr createScreenResources;
    ScreenBlockHandlerProcPtr BlockHandler;
    void *driver;

    DamagePtr damage;
    Bool dirty_enabled;

    uint32_t cursor_width, cursor_height;
    dummy_colors colors[256];
    pointer* FBBase;

} faux11Rec, *faux11Ptr;

#define faux11PTR(p) ((faux11Ptr)((p)->driverPrivate))
