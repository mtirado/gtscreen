/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */

#ifndef LINUX_DRM_H__
#define LINUX_DRM_H__

#include <stdint.h>
#include <stddef.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

/* represents a single "dumb" drm buffer */
struct simple_drmbuffer
{
	/* drm resource cleanup
	 * XXX maybe we dup this fd so user doesn't need to hold it? */
	/* int card_fd; */
	uint32_t drm_id;
	uint32_t fb_id;

	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t bpp;
	char    *addr;
	size_t   size;
};

/* we need to hold state for vt switching to reconnect crtc */
/* XXX i know this is really hacky right now, hold on... */
struct drm_state
{
	struct simple_drmbuffer *sfb;
	struct drm_mode_get_connector *conn;
	int conn_modeidx;
	/*struct drm_mode_modeinfo *mode;*/
	/*struct drm_mode_card_res *res;*/
	int card_fd;
};

/* TODO: double buffer+vsync */

/* drm/drm_mode.h */
uint32_t drm_get_id(uint64_t addr,
		    uint32_t idx);
int card_set_master(int card_fd);
int card_drop_master(int card_fd);
struct drm_mode_card_res *alloc_mode_card_res(int fd);
struct drm_mode_get_connector *alloc_connector(int fd,
					     uint32_t conn_id);
struct drm_mode_modeinfo *get_connector_modeinfo(struct drm_mode_get_connector *conn,
						 uint32_t *count);
int print_mode_card_resources(struct drm_mode_card_res *res);
int print_mode_modeinfo(struct drm_mode_modeinfo *mode);
int print_connector(struct drm_mode_get_connector *conn);
int free_mode_card_res(struct drm_mode_card_res *res);
int free_connector(struct drm_mode_get_connector *conn);

/* connect framebuffer to crtc, not sure on this yet may change XXX
 * XXX ->>  maybe this just takes a drm state since we must track that anyway
 * */
int connect_sfb(int card_fd,
		struct drm_mode_get_connector *conn,
		struct drm_mode_modeinfo *mode,
		struct simple_drmbuffer *sfb);
/* TODO mode/pixmap picker, width/height/bpp/etc as hints */
struct simple_drmbuffer *alloc_sfb(int card_fd,
				    uint32_t width,
				    uint32_t height,
				    uint32_t depth,
				    uint32_t bpp);

int destroy_sfb(int card_fd, struct simple_drmbuffer *fb);

void drm_set_state(struct drm_state *state);
struct drm_state *drm_get_state();
int drm_acquire_signal();
int drm_release_signal();
#endif
