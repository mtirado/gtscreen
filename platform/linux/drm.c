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
 * for pageflipping see libdrm, there's a test program tests/modetest/modetest.c
 *
 */

#define _GNU_SOURCE
#include "../../defines.h"
#include "drm.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define STRERR strerror(errno)

#include "../../spr16.h"
#include "../../screen.h"

extern struct server g_server;
extern struct screen *g_main_screen;

/* drm kernel structs use __u64 for pointer types: drm_mode.h */
#if (PTRBITCOUNT == 32)
	#define drm_to_ptr(ptr)   ((void *)(uint32_t)(ptr))
	#define drm_from_ptr(ptr) ((uint32_t)(ptr))
#elif (PTRBITCOUNT == 64)
	#define drm_to_ptr(ptr)   ((void *)(uint64_t)(ptr))
	#define drm_from_ptr(ptr) ((uint64_t)(ptr))
#else
	#error "PTRBITCOUNT is undefined"
#endif
#define drm_alloc(size) (drm_from_ptr(calloc(1,size)))

/* get id out of drm_id_ptr */
static uint32_t drm_get_id(uint64_t addr, uint32_t idx)
{
	return ((uint32_t *)drm_to_ptr(addr))[idx];
}

static int free_mode_card_res(struct drm_mode_card_res *res)
{
	if (!res)
		return -1;
	if (res->fb_id_ptr)
		free(drm_to_ptr(res->fb_id_ptr));
	if (res->crtc_id_ptr)
		free(drm_to_ptr(res->crtc_id_ptr));
	if (res->encoder_id_ptr)
		free(drm_to_ptr(res->encoder_id_ptr));
	if (res->connector_id_ptr)
		free(drm_to_ptr(res->connector_id_ptr));
	free(res);
	return 0;
}

static struct drm_mode_card_res *alloc_mode_card_res(int fd)
{
	struct drm_mode_card_res res;
	struct drm_mode_card_res *ret;
	uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;

	memset(&res, 0, sizeof(struct drm_mode_card_res));
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) {
		printf("ioctl(DRM_IOCTL_MODE_GETRESOURCES, &res): %s\n", STRERR);
		return NULL;
	}
	if (res.count_fbs > MAX_FBS
			|| res.count_crtcs > MAX_CRTCS
			|| res.count_encoders > MAX_ENCODERS
			|| res.count_connectors > MAX_CONNECTORS) {
		printf("resource limit reached, see defines.h\n");
		return NULL;
	}
	if (res.count_fbs) {
		res.fb_id_ptr = drm_alloc(sizeof(uint32_t)*res.count_fbs);
		if (!res.fb_id_ptr)
			goto alloc_err;
	}
	if (res.count_crtcs) {
		res.crtc_id_ptr = drm_alloc(sizeof(uint32_t)*res.count_crtcs);
		if (!res.crtc_id_ptr)
			goto alloc_err;
	}
	if (res.count_encoders) {
		res.encoder_id_ptr = drm_alloc(sizeof(uint32_t)*res.count_encoders);
		if (!res.encoder_id_ptr)
			goto alloc_err;
	}
	if (res.count_connectors) {
		res.connector_id_ptr = drm_alloc(sizeof(uint32_t)*res.count_connectors);
		if (!res.connector_id_ptr)
			goto alloc_err;
	}
	count_fbs = res.count_fbs;
	count_crtcs = res.count_crtcs;
	count_encoders = res.count_encoders;
	count_connectors = res.count_connectors;

	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_GETRESOURCES, &res): %s\n", STRERR);
		goto free_err;
	}

	if (count_fbs != res.count_fbs
			|| count_crtcs != res.count_crtcs
			|| count_encoders != res.count_encoders
			|| count_connectors != res.count_connectors) {
		errno = EAGAIN;
		goto free_err;
	}

	ret = calloc(1, sizeof(struct drm_mode_card_res));
	if (!ret)
		goto alloc_err;

	memcpy(ret, &res, sizeof(struct drm_mode_card_res));
	return ret;

alloc_err:
	errno = ENOMEM;
free_err:
	free(drm_to_ptr(res.fb_id_ptr));
	free(drm_to_ptr(res.crtc_id_ptr));
	free(drm_to_ptr(res.connector_id_ptr));
	free(drm_to_ptr(res.encoder_id_ptr));
	return NULL;
}

int print_mode_card_resources(struct drm_mode_card_res *res)
{
	if (!res)
		return -1;

	printf("min_width:        %d\n", res->min_width);
	printf("min_height:       %d\n", res->min_height);
	printf("max_width:        %d\n", res->max_width);
	printf("max_height:       %d\n", res->max_height);
	printf("count_fbs:        %d\n", res->count_fbs);
	printf("count_crtcs:      %d\n", res->count_crtcs);
	printf("count_encoders:   %d\n", res->count_encoders);
	printf("count_connectors: %d\n", res->count_connectors);

	return 0;
}

static struct drm_mode_get_connector *alloc_connector(int fd, uint32_t conn_id)
{
	struct drm_mode_get_connector conn;
	struct drm_mode_get_connector *ret;
	uint32_t count_modes, count_props, count_encoders;

	memset(&conn, 0, sizeof(struct drm_mode_get_connector));
	conn.connector_id = conn_id;

	if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn): %s\n", STRERR);
		return NULL;
	}
	if (conn.count_modes > MAX_MODES
			|| conn.count_props > MAX_PROPS
			|| conn.count_encoders > MAX_ENCODERS) {
		printf("resource limit reached, see defines.h\n");
		return NULL;
	}
	if (conn.count_modes) {
		conn.modes_ptr = drm_alloc(sizeof(struct drm_mode_modeinfo)
					   * conn.count_modes);
		if (!conn.modes_ptr)
			goto alloc_err;
	}
	if (conn.count_props) {
		conn.props_ptr = drm_alloc(sizeof(uint32_t)*conn.count_props);
		if (!conn.props_ptr)
			goto alloc_err;
		conn.prop_values_ptr = drm_alloc(sizeof(uint64_t)*conn.count_props);
		if (!conn.prop_values_ptr)
			goto alloc_err;
	}
	if (conn.count_encoders) {
		conn.encoders_ptr = drm_alloc(sizeof(uint32_t)*conn.count_encoders);
		if (!conn.encoders_ptr)
			goto alloc_err;
	}
	count_modes = conn.count_modes;
	count_props = conn.count_props;
	count_encoders = conn.count_encoders;

	if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn): %s\n", STRERR);
		goto free_err;
	}

	if (count_modes != conn.count_modes
			|| count_props != conn.count_props
			|| count_encoders != conn.count_encoders) {
		errno = EAGAIN;
		goto free_err;
	}

	ret = calloc(1, sizeof(struct drm_mode_get_connector));
	if (!ret)
		goto alloc_err;

	memcpy(ret, &conn, sizeof(struct drm_mode_get_connector));
	return ret;

alloc_err:
	errno = ENOMEM;
free_err:
	free(drm_to_ptr(conn.modes_ptr));
	free(drm_to_ptr(conn.props_ptr));
	free(drm_to_ptr(conn.encoders_ptr));
	free(drm_to_ptr(conn.prop_values_ptr));
	return NULL;
}

static struct drm_mode_modeinfo *get_connector_modeinfo(struct drm_mode_get_connector *conn,
						 uint32_t *count)
{
	if (!conn || !count)
		return NULL;
	*count = conn->count_modes;
	return drm_to_ptr(conn->modes_ptr);

}

int print_mode_modeinfo(struct drm_mode_modeinfo *mode)
{
	if (!mode)
		return -1;

	printf("name:         %s\n", mode->name);
	printf("clock:        %d\n", mode->clock);
	printf("hdisplay:     %d\n", mode->hdisplay);
	printf("vdisplay:     %d\n", mode->vdisplay);
	printf("hsync_start:  %d\n", mode->hsync_start);
	printf("hsync_end:    %d\n", mode->hsync_end);
	printf("vsync_start:  %d\n", mode->vsync_start);
	printf("vsync_end:    %d\n", mode->vsync_end);
	printf("htotal:       %d\n", mode->htotal);
	printf("hskew:        %d\n", mode->hskew);
	printf("vtotal:       %d\n", mode->vtotal);
	printf("vscan:        %d\n", mode->vscan);
	printf("vrefresh:     %d\n", mode->vrefresh);
	printf("flags:        %d\n", mode->flags);
	printf("type:         %d\n", mode->type);

	return 0;
}

int print_connector(struct drm_mode_get_connector *conn)
{
	if (!conn)
		return -1;
	/* TODO loop through ptrs */
	printf("connection:        %d\n", conn->connection);
	printf("subpixel:          %d\n", conn->subpixel);
	printf("mm_width:          %d\n", conn->mm_width);
	printf("mm_height:         %d\n", conn->mm_height);
	printf("count_modes:       %d\n", conn->count_modes);
	printf("count_props:       %d\n", conn->count_props);
	printf("count_encoders:    %d\n", conn->count_encoders);
	printf("encoder_id:        %d\n", conn->encoder_id);
	printf("connector_type:    %d\n", conn->connector_type);
	printf("connector_type_id: %d\n", conn->connector_type_id);
	printf("encoders_ptr:      %p\n", drm_to_ptr(conn->encoders_ptr));
	printf("modes_ptr:         %p\n", drm_to_ptr(conn->modes_ptr));
	printf("props_ptr:         %p\n", drm_to_ptr(conn->props_ptr));
	printf("prop_values_ptr:   %p\n", drm_to_ptr(conn->prop_values_ptr));
	return 0;
}

static int free_connector(struct drm_mode_get_connector *conn)
{
	if (!conn)
		return -1;
	if (conn->modes_ptr)
		free(drm_to_ptr(conn->modes_ptr));
	if (conn->props_ptr)
		free(drm_to_ptr(conn->props_ptr));
	if (conn->encoders_ptr)
		free(drm_to_ptr(conn->encoders_ptr));
	if (conn->prop_values_ptr)
		free(drm_to_ptr(conn->prop_values_ptr));
	free(conn);
	return 0;
}

int drm_kms_print_modes(char *devpath)
{
	struct drm_mode_card_res *res = NULL;
	struct drm_mode_get_connector *conn = NULL;
	struct drm_mode_modeinfo *modes = NULL;
	uint32_t mode_count;
	int card_fd;
	int ret = -1;
	uint32_t i;

	card_fd = open(devpath, O_RDWR|O_CLOEXEC);
	if (card_fd == -1) {
		printf("open(%s): %s\n", devpath, STRERR);
		return -1;
	}

	res = alloc_mode_card_res(card_fd);
	if (!res) {
		printf("unable to create drm structure\n");
		goto free_ret;
	}

	printf("--- Card 0 Resources ----------------------\n");
	print_mode_card_resources(res);
	for (i = 0; i < res->count_connectors; ++i)
	{
		uint32_t conn_id;
		uint32_t z;
		mode_count = 0;
		conn_id = drm_get_id(res->connector_id_ptr, i);
		conn = alloc_connector(card_fd, conn_id);
		if (!conn) {
			printf("unable to create drm structure\n");
			goto free_ret;
		}
		printf("--- Connector %d --------------------------\n", conn_id);
		print_connector(conn);
		modes = get_connector_modeinfo(conn, &mode_count);
		printf("Modes: %d\n", mode_count);
		for (z = 0; z < mode_count; ++z)
		{
			printf("    [%d] %dx%d@%dhz\n", z,
					modes[z].hdisplay,
					modes[z].vdisplay,
					modes[z].vrefresh);
		}
	}

	ret = 0;
free_ret:
	free_connector(conn);
	free_mode_card_res(res);
	close(card_fd);
	return ret;
}


#ifdef SPR16_SERVER

/* TODO split this function up into refresh display, and connect sfb */
static int drm_kms_connect_sfb(struct drm_kms *self)
{
	struct drm_display *display = &self->display;
	struct drm_mode_get_connector *conn;
	struct drm_mode_modeinfo *cur_mode;
	struct drm_mode_get_encoder *encoder;
	struct drm_mode_crtc *crtc;
	if (!display || !display->conn || !display->cur_mode || !self->sfb)
		return -1;

	conn = display->conn;
	cur_mode = display->cur_mode;
	encoder = &self->display.encoder;
	crtc = &self->display.crtc;
	memset(crtc, 0, sizeof(struct drm_mode_crtc));
	memset(encoder,  0, sizeof(struct drm_mode_get_encoder));

	/* TODO i think we only need to refresh structures if the connector/crtc?
	 * has changed, not too sure about that right now, swapping displays
	 * could potentially change resolutions so we really should recreate
	 * sfb's altogether once i figure out a good way to detect this
	 */

	/* XXX: there can be multiple encoders, have not investigated this much */
	encoder->encoder_id = conn->encoder_id;
	if (ioctl(self->card_fd, DRM_IOCTL_MODE_GETENCODER, encoder) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_GETENCODER): %s\n", STRERR);
		return -1;
	}

	crtc->crtc_id = encoder->crtc_id;
	if (ioctl(self->card_fd, DRM_IOCTL_MODE_GETCRTC, crtc) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_GETCRTC): %s\n", STRERR);
		return -1;
	}

	/* set crtc mode */
	crtc->fb_id = self->sfb->fb_id;
	crtc->set_connectors_ptr = drm_from_ptr((void *)&conn->connector_id);
	crtc->count_connectors = 1;
	crtc->mode = *cur_mode;
	/*printf("\nsetting mode:\n\n");
	print_mode_modeinfo(cur_mode);*/
	crtc->mode_valid = 1;
	if (ioctl(self->card_fd, DRM_IOCTL_MODE_SETCRTC, crtc) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_SETCRTC): %s\n", STRERR);
		return -1;
	}
	return 0;
}

/* TODO test failure resource cleanup */
static struct drm_buffer *alloc_sfb(int card_fd,
			     uint32_t width,
			     uint32_t height,
			     uint32_t depth,
			     uint32_t bpp)
{
	struct drm_mode_create_dumb cdumb;
	struct drm_mode_map_dumb    moff;
	struct drm_mode_fb_cmd      cmd;
	struct drm_buffer *ret;
	void  *fbmap;

	memset(&cdumb, 0, sizeof(cdumb));
	memset(&moff,  0, sizeof(moff));
	memset(&cmd,   0, sizeof(cmd));

	/* create dumb buffer */
	cdumb.width  = width;
	cdumb.height = height;
	cdumb.bpp    = bpp;
	cdumb.flags  = 0;
	cdumb.pitch  = 0;
	cdumb.size   = 0;
	cdumb.handle = 0;
	if (ioctl(card_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cdumb) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_CREATE_DUMB): %s\n", STRERR);
		return NULL;
	}
	/* add framebuffer object */
	cmd.width  = cdumb.width;
	cmd.height = cdumb.height;
	cmd.bpp    = cdumb.bpp;
	cmd.pitch  = cdumb.pitch;
	cmd.depth  = depth;
	cmd.handle = cdumb.handle;
	if (ioctl(card_fd, DRM_IOCTL_MODE_ADDFB, &cmd) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_ADDFB): %s\n", STRERR);
		ioctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &cdumb.handle);
		return NULL;
	}
	/* get mmap offset */
	moff.handle = cdumb.handle;
	if (ioctl(card_fd, DRM_IOCTL_MODE_MAP_DUMB, &moff) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_MAP_DUMB): %s\n", STRERR);
		ioctl(card_fd, DRM_IOCTL_MODE_RMFB, &cmd.fb_id);
		ioctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &cdumb.handle);
		return NULL;
	}
	/* map it! */
	fbmap = mmap(0, (size_t)cdumb.size, PROT_READ|PROT_WRITE,
			MAP_SHARED, card_fd, (off_t)moff.offset);
	if (fbmap == MAP_FAILED) {
		printf("framebuffer mmap failed: %s\n", STRERR);
		ioctl(card_fd, DRM_IOCTL_MODE_RMFB, &cmd.fb_id);
		ioctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &cdumb.handle);
		return NULL;
	}

	ret = calloc(1, sizeof(struct drm_buffer));
	if (!ret) {
		printf("-ENOMEM\n");
		munmap(fbmap, cdumb.size);
		ioctl(card_fd, DRM_IOCTL_MODE_RMFB, &cmd.fb_id);
		ioctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &cdumb.handle);
		return NULL;
	}
	ret->addr     = fbmap;
	ret->size     = cdumb.size;
	ret->width    = cdumb.width;
	ret->height   = cdumb.height;
	ret->bpp      = cdumb.bpp;
	ret->depth    = cmd.depth;
	ret->fb_id    = cmd.fb_id;
	ret->drm_id   = cdumb.handle;
	memset(fbmap, 0x27, cdumb.size);
	return ret;
}

static int destroy_sfb(int card_fd, struct drm_buffer *sfb)
{
	if (!sfb)
		return -1;

	if (munmap(sfb->addr, sfb->size) == -1)
		printf("munmap: %s\n", STRERR);
	if (ioctl(card_fd, DRM_IOCTL_MODE_RMFB, &sfb->fb_id))
		printf("ioctl(DRM_IOCTL_MODE_RMFB): %s\n", STRERR);
	if (ioctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &sfb->drm_id))
		printf("ioctl(DRM_IOCTL_MODE_DESTROY_DUMB): %s\n", STRERR);
	free(sfb);
	return 0;
}

static int card_set_master(int card_fd)
{
	if (ioctl(card_fd, DRM_IOCTL_SET_MASTER, 0)) {
		printf("ioctl(DRM_IOCTL_SET_MASTER, 0): %s\n", STRERR);
		return -1;
	}
	return 0;
}
static int card_drop_master(int card_fd)
{
	if (ioctl(card_fd, DRM_IOCTL_DROP_MASTER, 0)) {
		printf("ioctl(DRM_IOCTL_DROP_MASTER, 0): %s\n", STRERR);
		return -1;
	}
	return 0;
}

/* TODO multiple states needed for managing multiple cards */
int drm_acquire_signal(struct drm_kms *self)
{
	if (card_set_master(self->card_fd)) {
		printf("set master(acquire signal)\n");
		return -1;
	}
	/* TODO, plugging in a new display probably screws things up,
	 * should refresh the entire display struct here for new connectors.
	 */
	if (drm_kms_connect_sfb(self) == -1) {
		printf("unable to reconnect drm buffer\n");
		return -1;
	}
	spr16_server_activate();

	return 0;
}

int drm_release_signal(struct drm_kms *self)
{
	spr16_server_deactivate();
	if (card_drop_master(self->card_fd)) {
		printf("-- drop master sig2 --\n");
		return -1;
	}
	return 0;
}

static int drm_display_destroy(struct drm_display *display)
{
	if (display->conn)
		free_connector(display->conn);
	memset(display, 0, sizeof(struct drm_display));
	return 0;
}



int drm_kms_destroy(struct drm_kms *self)
{
	if (self->sfb)
		destroy_sfb(self->card_fd, self->sfb);
	if (self->res)
		free_mode_card_res(self->res);
	drm_display_destroy(&self->display);

	close(self->card_fd);
	memset(self, 0, sizeof(struct drm_kms));
	free(self);
	return 0;
}

/* returns the closest <= match preference on width
 * refresh is matched to closest value */
static int get_mode_idx(struct drm_mode_modeinfo *modes,
			uint16_t count,
			uint16_t width,
			uint16_t height,
			uint16_t refresh)
{
	int i;
	int pick = -1;
	if (width == 0)
		width = 0xffff;
	if (height == 0)
		height = 0xffff;
	for (i = 0; i < count; ++i)
	{
		if (modes[i].hdisplay > width || modes[i].vdisplay > height)
			continue;
		/* must divide evenly by 16 */
		if (modes[i].hdisplay % 16 == 0) {
			if (pick < 0) {
				pick = i;
				continue;
			}
			if (modes[i].hdisplay > modes[pick].hdisplay)
				pick = i;
			else if (modes[i].vdisplay > modes[pick].vdisplay)
				pick = i;
			else if (modes[i].hdisplay == modes[pick].hdisplay
					&& modes[i].vdisplay == modes[pick].vdisplay) {
				if (abs(refresh - modes[i].vrefresh)
					  < abs(refresh - modes[pick].vrefresh)) {
					pick = i;
				}
			}
		}
	}
	if (pick < 0) {
		printf("could not find any usable modes for (%dx%d@%dhz)\n",
				width, height, refresh);
		return -1;
	}
	return pick;
}



/* TODO figure out what changes when displays are unplugged and new ones plugged in.
 * if resolutions are different, does the connector_id change? if so we can just check
 * the id to determine if a new display is plugged in, and realloc struct in that case.
 * if only the modes change, well maybe stop using alloc for static sized arrays.
 * maybe nothing changes in the crtc? i haven't looked at the source or tested to know.
 *
 */
static int drm_display_load(struct drm_kms *self,
		     uint16_t req_width,
		     uint16_t req_height,
		     uint16_t req_refresh,
		     struct drm_display *out)
{
	uint32_t conn_id;
	int idx = -1;

	/* FIXME uses primary connector? "0" */
	conn_id = drm_get_id(self->res->connector_id_ptr, 0);
	out->conn = alloc_connector(self->card_fd, conn_id);
	if (!out->conn) {
		printf("unable to create drm connector structure\n");
		return -1;
	}

	out->conn_id = conn_id;
	out->modes = get_connector_modeinfo(out->conn, &out->mode_count);
	idx = get_mode_idx(out->modes, out->mode_count,
			   req_width, req_height, req_refresh);
	if (idx < 0)
		goto free_err;

	out->cur_mode_idx = (uint32_t)idx;
	out->cur_mode = &out->modes[out->cur_mode_idx];
	return 0;
free_err:
	drm_display_destroy(out);
	return -1;
}

struct drm_kms *drm_mode_create(char *devname,
				int no_connect,
				uint16_t req_width,
				uint16_t req_height,
				uint16_t req_refresh)
{
	char devpath[128];
	struct drm_kms *self;
	struct drm_mode_modeinfo *cur_mode;
	int card_fd;

	snprintf(devpath, sizeof(devpath), "/dev/dri/%s", devname);
	card_fd = open(devpath, O_RDWR|O_CLOEXEC);
	if (card_fd == -1) {
		printf("open(%s): %s\n", devpath, STRERR);
		return NULL;
	}
	if (card_set_master(card_fd)) {
		printf("card_set_master failed\n");
		return NULL;
	}

	self = calloc(1, sizeof(struct drm_kms));
	if (!self)
		return NULL;

	self->card_fd = card_fd;
	self->res = alloc_mode_card_res(card_fd);
	if (!self->res) {
		printf("unable to create drm structure\n");
		goto free_err;
	}

	if (drm_display_load(self, req_width, req_height, req_refresh, &self->display)) {
		printf("drm_display_load failed\n");
		goto free_err;
	}
	cur_mode = self->display.cur_mode;
	printf("connector(%d) using mode[%d] (%dx%d@%dhz)\n",
				self->display.conn_id,
				self->display.cur_mode_idx,
				cur_mode->hdisplay,
				cur_mode->vdisplay,
				cur_mode->vrefresh);


	/* buffer pitch must divide evenly by 16,
	 * TODO check against bpp here when that is variable instead of 32 */
	self->sfb = alloc_sfb(card_fd, cur_mode->hdisplay, cur_mode->vdisplay, 24, 32);
	if (!self->sfb) {
		printf("alloc_sfb failed\n");
		goto free_err;
	}

	if (!no_connect && drm_kms_connect_sfb(self)) {
		printf("drm_kms_connect_sfb failed\n");
		goto free_err;
	}
	return self;

free_err:
	drm_kms_destroy(self);
	return NULL;
}

#endif

int drm_prime_export_fd(int card_fd, struct drm_buffer *buffer, int *out_prime_fd)
{
	struct drm_prime_handle prime_handle;
	int r;

	memset(&prime_handle, 0, sizeof(struct drm_prime_handle));
	prime_handle.handle = buffer->drm_id;
	prime_handle.flags = DRM_CLOEXEC | DRM_RDWR;
	do {
		r = ioctl(card_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_handle);
	} while (r == -1 && errno == EINTR);
	if (r) {
		printf("ioctl(DRM_IOCTL_PRIME_HANDLE_TO_FD): %s\n", strerror(errno));
		return -1;
	}
	*out_prime_fd = prime_handle.fd;
	return 0;
}

int drm_prime_import_fd(int card_fd, int prime_fd, uint32_t *out_gem_handle)
{
	struct drm_prime_handle prime_handle;
	int r;

	memset(&prime_handle, 0, sizeof(struct drm_prime_handle));
	prime_handle.fd = prime_fd;
	do {
		r = ioctl(card_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_handle);
	} while (r == -1 && errno == EINTR);
	if (r) {
		printf("ioctl(DRM_IOCTL_PRIME_HANDLE_TO_FD): %s\n", strerror(errno));
		return -1;
	}
	*out_gem_handle = prime_handle.handle;
	printf("imported handle = %d\n", prime_handle.handle);
	return 0;
}

