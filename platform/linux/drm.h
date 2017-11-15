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
 */

#ifndef LINUX_DRM_H__
#define LINUX_DRM_H__

#include <stdint.h>
#include <stddef.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include "../fdpoll-handler.h"

struct drm_buffer
{
	uint32_t drm_id;
	uint32_t fb_id;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t bpp;
	char    *addr;
	size_t   size;
};

struct drm_display
{
	struct drm_mode_get_encoder encoder;
	struct drm_mode_crtc crtc;
	struct drm_mode_get_connector *conn; /* do we need array for multi-screen? */
	struct drm_mode_modeinfo *modes; /* these both point to conn's mode array */
	struct drm_mode_modeinfo *cur_mode;
	uint32_t cur_mode_idx;
	uint32_t mode_count;
	uint32_t conn_id;
};

struct drm_kms
{
	struct drm_display display;
	struct drm_buffer *sfb;
	struct drm_mode_card_res *res;
	int card_fd;
};


struct drm_kms *drm_mode_create(char *devname,
				int no_connect,
				uint16_t req_width,
				uint16_t req_height,
				uint16_t req_refresh);

int drm_kms_destroy(struct drm_kms *self);
int drm_acquire_signal(struct drm_kms *self);
int drm_release_signal(struct drm_kms *self);
int drm_kms_print_modes(char *devpath);
int drm_prime_import_fd(int card_fd, int prime_fd, uint32_t *out_gem_handle);
int drm_prime_export_fd(int card_fd, struct drm_buffer *buffer, int *out_prime_fd);
char *drm_gem_mmap_handle(int card_fd, size_t length, off_t map_offset);

#endif
