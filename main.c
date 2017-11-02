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
 */

/*
 *  TODO: hmm probably should move this into platform/linux/
 *  evdev SYN_DROPPED
 *  i'm fine with evqueue assuming EV_MAX*2 size (*2 accounts for syn_dropped)
 *
 */

#define _GNU_SOURCE
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <malloc.h>
#include <signal.h>
#include <stdlib.h>

#include "defines.h"
#include "spr16.h"
#include "screen.h"
#include "platform/fdpoll-handler.h"
#include "platform/linux/platform.h"
#include "platform/linux/vt.h"
#include "platform/linux/fb.h"
#define STRERR strerror(errno)

sig_atomic_t g_running;
struct server_options g_srv_opts;
struct drm_kms *g_card0;

/* reset on fatal signals, sigkill screws us up still :( */
void sig_func(int signum)
{
	switch (signum)
	{
	case SIGQUIT:
	case SIGINT:
	case SIGHUP:
	case SIGTERM:
		g_running = 0;
		return;
	default:
		vt_shutdown();
		printf("killed by signal(%d): %s\n", signum, strsignal(signum));
		_exit(-1);
	}
}

static void sig_setup()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_func; sigaction(SIGTERM, &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGINT,  &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGILL,  &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGABRT, &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGQUIT, &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGHUP,  &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGFPE,  &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGSEGV, &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGALRM, &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGBUS,  &sa, NULL);
	sa.sa_handler = sig_func; sigaction(SIGSYS,  &sa, NULL);

	sa.sa_handler = SIG_IGN; sigaction(SIGPIPE, &sa, NULL);
}

int server_main(struct fdpoll_handler *fdpoll, struct drm_kms *card)
{
	struct server_context *ctx;
	struct spr16_framebuffer fb;
	memset(&fb, 0, sizeof(struct spr16_framebuffer));
	fb.width  = card->sfb->width;
	fb.height = card->sfb->height;
	fb.bpp    = card->sfb->bpp;
	fb.addr   = card->sfb->addr;
	fb.size   = card->sfb->size;

	/*K_XLATE, or K_MEDIUMRAW for keycodes, RAW is 8 bits*/
	if (vt_init(0, K_XLATE))
		return -1;
	if (atexit(vt_shutdown))
		return -1;
	sig_setup();

	ctx = spr16_server_init(fdpoll, &fb);
	if (ctx == NULL) {
		printf("server init failed\n");
		return -1;
	}

	/* for vblank handler, and future pageflipping */
	if (fdpoll_handler_add(fdpoll, card->card_fd, FDPOLLIN, fb_drm_fd_callback, ctx)){
		printf("fdpoll_handler_add(%d) failed\n", card->card_fd);
		goto err;
	}

	while (g_running)
	{
		if (spr16_server_update(ctx)) {
			printf("spr16 update error\n");
			goto err;
		}
	}

	spr16_server_shutdown(ctx);
	return 0;

err:
	spr16_server_shutdown(ctx);
	return -1;
}

int read_environ(struct server_options *srv_opts)
{
	char *estr = NULL;
	char *err = NULL;
	int vscroll_amount     = 30;
	uint16_t pointer_accel = 0;
	uint16_t req_width     = 0;
	uint16_t req_height    = 0;
	uint16_t req_refresh   = 60;

	memset(srv_opts, 0, sizeof(struct server_options));

	estr = getenv("SPR16_VSCROLL_AMOUNT");
	if (estr != NULL) {
		errno = 0;
		vscroll_amount = strtol(estr, &err, 10);
		if (err == NULL || *err || errno) {
			printf("erroneous environ SPR16_VSCROLL_AMOUNT\n");
				return -1;
		}
		if (vscroll_amount < -50)
			vscroll_amount = -50;
		else if (vscroll_amount > 50)
			vscroll_amount = 50;
	}

	estr = getenv("SPR16_SCREEN_WIDTH");
	if (estr != NULL) {
		errno = 0;
		req_width = strtol(estr, &err, 10);
		if (err == NULL || *err || errno || req_width == 0) {
			printf("erroneous environ SPR16_SCREEN_WIDTH\n");
				return -1;
		}
		if (req_width < 256)
			req_width = 256;
	}
	estr = getenv("SPR16_SCREEN_HEIGHT");
	if (estr != NULL) {
		errno = 0;
		req_height = strtol(estr, &err, 10);
		if (err == NULL || *err || errno || req_height == 0) {
			printf("erroneous environ SPR16_SCREEN_HEIGHT\n");
				return -1;
		}
		if (req_height < 128)
			req_height = 128;
	}
	estr = getenv("SPR16_SCREEN_REFRESH");
	if (estr != NULL) {
		errno = 0;
		req_refresh = strtol(estr, &err, 10);
		if (err == NULL || *err || errno || req_refresh == 0) {
			printf("erroneous environ SPR16_SCREEN_REFRESH\n");
				return -1;
		}
	}
	estr = getenv("SPR16_POINTER_ACCEL");
	if (estr != NULL) {
		errno = 0;
		pointer_accel = strtol(estr, &err, 10);
		if (err == NULL || *err || errno) {
			printf("erroneous environ SPR16_POINTER_ACCEL\n");
				return -1;
		}
	}
	srv_opts->pointer_accel   = pointer_accel;
	srv_opts->vscroll_amount  = vscroll_amount;
	srv_opts->request_width   = req_width;
	srv_opts->request_height  = req_height;
	srv_opts->request_refresh = req_refresh;
	return 0;
}


static void print_usage()
{
	printf("\n");
	printf("usage: gtscreen <arguments>\n");
	printf("\n");
	printf("[arguments]\n");
	printf("    --printmodes    print all connectors and modes\n");
	printf("\n");
	printf("[environment variables]\n");
	printf("    SPR16_SCREEN_WIDTH        preferred screen width\n");
	printf("    SPR16_SCREEN_HEIGHT       preferred screen height\n");
	printf("    SPR16_SCREEN_REFRESH      target refresh rate\n");
	printf("    SPR16_VSCROLL_AMOUNT      vertical scroll minimum\n");
	printf("    SPR16_POINTER_ACCEL       pointer acceleration\n");
	printf("    SPR16_TRACKPAD            surface acts as trackpad\n");
	printf("\n");
}

int read_args(int argc, char *argv[])
{
	int i;
	if (argc <= 1)
		return 0;
	for (i = 1; i < argc; ++i)
	{
		if (strncmp("--printmodes", argv[i], 13) == 0) {
			/* FIXME */
			drm_kms_print_modes("/dev/dri/card0");
			_exit(0);
			return -1;
		}
		else {
			print_usage();
			return -1;
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct fdpoll_handler *fdpoll;
	int ret = -1;

	g_running = 1;
	memset(&g_srv_opts, 0, sizeof(struct server_options));

	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	if (read_environ(&g_srv_opts))
		return -1;
	if (read_args(argc, argv))
		return -1;

	fdpoll = fdpoll_handler_create(MAX_FDPOLL_HANDLER, 1);
	if (fdpoll == NULL) {
		printf("fdpoll_handler_create(%d, 1) failed\n", MAX_FDPOLL_HANDLER);
		return -1;
	}

	/* card0 must be set before any SIGUSR1/2's might be sent! */
	g_card0 = drm_mode_create("card0", g_srv_opts.request_width,
					   g_srv_opts.request_height,
					   g_srv_opts.request_refresh);
	if (g_card0 == NULL) {
		printf("drm_mode_create failed\n");
		return -1;
	}

	ret = server_main(fdpoll, g_card0);
	drm_kms_destroy(g_card0);
	return ret;
}
