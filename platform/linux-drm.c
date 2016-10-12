/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * linux-drm
 */
#include "../defines.h"
#include "linux-drm.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <malloc.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define STRERR strerror(errno)

/* remember state for vt switches */
struct drm_state g_state;

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
uint32_t drm_get_id(uint64_t addr, uint32_t idx)
{
	return ((uint32_t *)drm_to_ptr(addr))[idx];
}

/* TODO -- this needs to be improved, track all resources well.
 * we could have many crtcs or  with front+back buffers, etc  */
void drm_set_state(struct drm_state *state)
{
	memcpy(&g_state, state, sizeof(g_state));
}

struct drm_state *drm_get_state()
{
	return &g_state;
}

int card_set_master(int card_fd)
{
	if (ioctl(card_fd, DRM_IOCTL_SET_MASTER, 0) == -1) {
		printf("ioctl(DRM_IOCTL_SET_MASTER, 0): %s\n", STRERR);
		return -1;
	}
	return 0;
}

int card_drop_master(int card_fd)
{
	if (ioctl(card_fd, DRM_IOCTL_DROP_MASTER, 0) == -1) {
		printf("ioctl(DRM_IOCTL_DROP_MASTER, 0): %s\n", STRERR);
		return -1;
	}
	return 0;
}

int free_mode_card_res(struct drm_mode_card_res *res)
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

struct drm_mode_card_res *alloc_mode_card_res(int fd)
{
	struct drm_mode_card_res res;
	struct drm_mode_card_res *ret;
	uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;

	memset(&res, 0, sizeof(struct drm_mode_card_res));
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == -1) {
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

struct drm_mode_get_connector *alloc_connector(int fd, uint32_t conn_id)
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

struct drm_mode_modeinfo *get_connector_modeinfo(struct drm_mode_get_connector *conn,
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

int free_connector(struct drm_mode_get_connector *conn)
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

int connect_sfb(int card_fd,
		struct drm_mode_get_connector *conn,
		struct drm_mode_modeinfo *mode,
		struct simple_drmbuffer *sfb)
{
	struct drm_mode_get_encoder enc;
	struct drm_mode_crtc crtc;

	if (!conn || !sfb || !mode)
		return -1;

	memset(&enc,  0, sizeof(enc));
	memset(&crtc, 0, sizeof(crtc));

	/* get crtc id */
	enc.encoder_id = conn->encoder_id;
	if (ioctl(card_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_GETENCODER): %s\n", STRERR);
		return -1;
	}
	crtc.crtc_id = enc.crtc_id;
	if (ioctl(card_fd, DRM_IOCTL_MODE_GETCRTC, &crtc) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_GETCRTC): %s\n", STRERR);
		return -1;
	}
	/* set crtc mode */
	crtc.fb_id = sfb->fb_id;
	crtc.set_connectors_ptr = drm_from_ptr((void *)&conn->connector_id);
	crtc.count_connectors = 1;
	crtc.mode = *mode;
	/*printf("\nsetting mode:\n\n");
	print_mode_modeinfo(mode);*/
	crtc.mode_valid = 1;
	if (ioctl(card_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) == -1) {
		printf("ioctl(DRM_IOCTL_MODE_SETCRTC): %s\n", STRERR);
		return -1;
	}
	return 0;
}

/* TODO test failure resource cleanup */
struct simple_drmbuffer *alloc_sfb(int card_fd,
				    uint32_t width,
				    uint32_t height,
				    uint32_t depth,
				    uint32_t bpp)
{
	struct drm_mode_create_dumb cdumb;
	struct drm_mode_map_dumb    moff;
	struct drm_mode_fb_cmd      cmd;
	struct simple_drmbuffer *ret;
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

	ret = calloc(1, sizeof(struct simple_drmbuffer));
	if (!ret) {
		printf("-ENOMEM\n");
		munmap(fbmap, cdumb.size);
		ioctl(card_fd, DRM_IOCTL_MODE_RMFB, &cmd.fb_id);
		ioctl(card_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &cdumb.handle);
		return NULL;
	}
	ret->addr   = fbmap;
	ret->size   = cdumb.size;
	ret->width  = cdumb.width;
	ret->height = cdumb.height;
	ret->bpp    = cdumb.bpp;
	ret->depth  = cmd.depth;
	ret->fb_id  = cmd.fb_id;
	ret->drm_id = cdumb.handle;
	memset(fbmap, 0x27, cdumb.size);
	return ret;
}

int destroy_sfb(int card_fd, struct simple_drmbuffer *sfb)
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

/* TODO multiple states needed for managing multiple cards */
int drm_acquire_signal()
{
	struct drm_mode_modeinfo *modes;
	uint32_t modecount;
	if (card_set_master(g_state.card_fd)) {
		printf("set master(acquire signal)\n");
	}
	modes = get_connector_modeinfo(g_state.conn, &modecount);
	if (!modes || !modecount) {
		printf("drm_acquire_signal bad modeinfo\n");
		return -1;
	}
	if (connect_sfb(g_state.card_fd,
			g_state.conn,
			&modes[g_state.conn_modeidx],
			g_state.sfb) == -1) {
		printf("unable to reconnect drm buffer\n");
		return -1;
	}
	return 0;
}
int drm_release_signal()
{
	if (card_drop_master(g_state.card_fd)) {
		printf("-- drop master sig2 --\n");
	}
	return 0;
}

