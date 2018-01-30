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

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <memory.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../../screen.h"
#include "fb.h"

extern void x86_sse2_xmmcpy_128(char *dest, char *src, unsigned int count);
extern void x86_sse2_xmmcpy_512(char *dest, char *src, unsigned int count);
extern void x86_sse2_xmmcpy_1024(char *dest, char *src, unsigned int count);
extern void x86_slocpy_512(void *dest, void *src, unsigned int count);
#include <time.h>

static struct timespec bench_begin()
{
	struct timespec ret;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ret);
	return ret;
}

unsigned long usecs_elapsed(struct timespec tlast, struct timespec tcur)
{
	struct timespec elapsed;
	unsigned long usec;
	elapsed.tv_sec = tcur.tv_sec - tlast.tv_sec;
	if (!elapsed.tv_sec) {
		elapsed.tv_nsec = tcur.tv_nsec - tlast.tv_nsec;
		usec = elapsed.tv_nsec / 1000;
	}
	else {
		usec = ((1000000000 - tlast.tv_nsec) + tcur.tv_nsec) / 1000;
		usec += (elapsed.tv_sec-1) * 1000000;
	}
	return usec;
}
static void bench_end(struct timespec start)
{
	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC_RAW, &end);
	printf("benched usecs: %lu\n", usecs_elapsed(start, end));
}

/* now just assumes top left orientation :( */
static int copy_to_fb(struct spr16_framebuffer *fb,
		      struct client *cl,
		      struct spr16_msgdata_sync dmg)
{

	/* TODO < 8bpp support */
	const uint32_t grid_size = PIXL_ALIGN * (fb->bpp/8);
	const uint32_t weight = fb->bpp/8;
	uint32_t count;
	uint16_t x = dmg.xmin;
	uint16_t y = dmg.ymin;
	uint16_t width = dmg.xmax - dmg.xmin;
	uint16_t height = dmg.ymax - dmg.ymin;
	uint16_t i;
	struct timespec bench_timer;
	/* convert to bytes */
	width *= weight;
	x *= weight;

	count = width / grid_size;

	if (width % grid_size) {
		printf("drm copy align: %d\n", width % grid_size);
		return -1;
	}

	(void) bench_begin;
	(void) bench_end;
	(void) bench_timer;
	/*bench_timer = bench_begin();*/
	for (i = 0; i < height; ++i)
	{
		const uint32_t svoff = (((y + i) * fb->width) * weight) + x;
		const uint32_t cloff = (((y + i) * cl->sprite.width) * weight) + x;
		uint16_t z;
#if PIXL_ALIGN == 32
		(void)z;
		x86_sse2_xmmcpy_1024(fb->addr + svoff,
				    cl->sprite.shmem.addr + cloff,
				    count);
#else
		for (z = 0; z < count; ++z) {
			memcpy((fb->addr + svoff) + (z * grid_size),
				(cl->sprite.shmem.addr + cloff)	+ (z * grid_size),
				grid_size);
		}
#endif
		/*printf("sync(%d, %d, %d, %d) y=%d\n", x, y, width, height, i);*/
		/*x86_slocpy_512(fb->addr + svoff,
				    cl->sprite.shmem.addr + cloff,
				    count);*/
	}
	/*usleep(30000);
	bench_end(bench_timer);*/
	return 0;
}

int sync_dmg_to_fb(struct spr16_framebuffer *fb, struct client *cl)
{
	int i;

	if (cl->sprite.flags & SPRITE_FLAG_DIRECT_SHM) {
		/* no memcpy needed if client has a direct map */
		return 0;
	}

	/* maybe prefetch cl sprite here or something fancy like that? */
	for (i = 0; i < cl->dmg_count; ++i)
	{
		if (copy_to_fb(fb, cl, cl->dmg[i])) {
			return -1;
		}
	}
	memset(cl->dmg, 0, sizeof(cl->dmg));
	cl->dmg_count = 0;
	return 0;
}

int fb_drm_fd_callback(int fd, int event_flags, void *user_data)
{
	char buf[4096];
	struct drm_event *event;
	int r;
	unsigned int pos = 0;
	int has_syncd = 0;
	struct server_context *ctx = user_data;

	if (event_flags & (FDPOLLHUP | FDPOLLERR)) {
		printf("drm fd HUP/ERR\n");
		return FDPOLL_HANDLER_REMOVE;
	}

	do {
		r = read(fd, buf, sizeof(buf));
	} while (r == -1 && errno == EINTR);

	if (r < 0) {
		printf("drm_handle_events, read: %s\n", strerror(errno));
		return FDPOLL_HANDLER_REMOVE;
	}
	else if (r < (int)sizeof(struct drm_event)) {
		printf("drm_handle_events, read size error");
		return FDPOLL_HANDLER_REMOVE;
	}

	while (pos < (unsigned int)r)
	{
		if (pos > r - sizeof(struct drm_event)) {
			printf("event size error\n");
			return FDPOLL_HANDLER_REMOVE;
		}
		event = (struct drm_event *)(buf+pos);
		if (pos > r - event->length) {
			printf("event length error\n");
			return FDPOLL_HANDLER_REMOVE;
		}

		switch (event->type)
		{
		case DRM_EVENT_VBLANK:
			if (!has_syncd && ctx->main_screen) {
				struct client *cl;
				cl = ctx->main_screen->clients;
				if (cl && sync_dmg_to_fb(ctx->fb, cl) == 0) {
					spr16_send_ack(cl->socket, SPRITEACK_SYNC_VSYNC);
					has_syncd = 1;
				}
			}
			break;

		case DRM_EVENT_FLIP_COMPLETE:
				printf("DRM_EVENT_FLIP_COMPLETE\n");
			/*g_waiting_for_flip = 0; DO NOT exit while waiting for a flip*/
			break;

		default:
			printf("unknown drm event received: %d\n", event->type);
			break;
		}
		pos += event->length;
	}
	return FDPOLL_HANDLER_OK;
}

/* note: page flipping will require drm master, which is dropped when vt is switched
 * also, there is a race condition, if flip is issued and the program exits screen
 * will lock up since it's still being referenced mid switch, or something...
 * */
static int drm_vblank(struct drm_kms *self)
{
	struct drm_wait_vblank_request vblank;
	int retry;
	memset(&vblank, 0, sizeof(vblank));
	vblank.type = _DRM_VBLANK_RELATIVE | _DRM_VBLANK_EVENT;
	vblank.sequence = 1;

	for (retry = 5000; retry >= 0; --retry)
	{
		int r = ioctl(self->card_fd, DRM_IOCTL_WAIT_VBLANK, &vblank);
		if (r == 0)
			break;
		else if (r == -1 && errno == EINTR)
			continue;

		printf("ioctl(DRM_IOCTL_WAIT_VBLANK): %s\n", strerror(errno));
		return 0;
		/*return -1;*/
	}
	return 0;
}

int fb_sync_client(struct server_context *self, struct client *cl)
{

	if (cl->sync_flags & SPRITESYNC_FLAG_VBLANK) {
		if (spr16_server_is_active()) {
			return drm_vblank(self->card0);
		}
	}
	else if (cl->sync_flags & SPRITESYNC_FLAG_PAGE_FLIP) {
		printf("page flip not implemented\n");
		return -1;
	}
	else if (cl->sync_flags & SPRITESYNC_FLAG_ASYNC) {
		if (!spr16_server_is_active()) {
			return sync_dmg_to_fb(self->fb, cl);
		}
	}


	return 0;
}



