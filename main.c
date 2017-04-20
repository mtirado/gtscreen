/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * gtscreen - graphic type screen
 *
 */

/*
 *  TODO: evdev SYN_DROPPED
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
#include "platform/linux-drm.h"
#include "platform/linux-vt.h"
#include "protocol/spr16.h"
#define STRERR strerror(errno)

struct server g_server;
struct drm_state g_state;
sig_atomic_t g_running;

/* reset on fatal signals, sigkill screws us up still :( */
void sig_func(int signum)
{
	switch (signum)
	{
	case SIGQUIT:
	case SIGINT:
	case SIGHUP:
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

int exec_loop(int tty)
{
	int listen_socket;
	listen_socket = spr16_server_init(g_state.sfb->width,
					  g_state.sfb->height,
					  g_state.sfb->bpp);
	if (listen_socket == -1 ){
		printf("server init failed\n");
		return -1;
	}
	(void)tty;

	/* TODO turn off kbd if using evdev? */
	if (spr16_server_init_input()) {
		printf("input init failed\n");
		return -1;
	}
	while (g_running)
	{
		if (spr16_server_update(listen_socket)) {
			printf("spr16 update error\n");
			spr16_server_shutdown(listen_socket);
			return -1;
		}
	}
	spr16_server_shutdown(listen_socket);
	return 0;
}

int read_environ()
{
	char *estr = NULL;
	char *err = NULL;
	int vscroll_amount   = 1;
	uint16_t req_width   = 0;
	uint16_t req_height  = 0;
	uint16_t req_refresh = 60;

	estr = getenv("SPR16_VSCROLL");
	if (estr != NULL) {
		errno = 0;
		vscroll_amount = strtol(estr, &err, 10);
		if (err == NULL || *err || errno || vscroll_amount == 0) {
			printf("erroneous environ SPR16_VSCROLL\n");
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

	g_server.vscroll_amount  = vscroll_amount;
	g_server.request_width   = req_width;
	g_server.request_height  = req_height;
	g_server.request_refresh = req_refresh;
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

static int print_modes()
{
	struct drm_mode_card_res *res = NULL;
	struct drm_mode_get_connector *conn = NULL;
	struct drm_mode_modeinfo *modes = NULL;
	uint32_t mode_count;
	int card_fd;
	int ret = 0;
	uint32_t i;

	card_fd = open("/dev/dri/card0", O_RDWR|O_CLOEXEC);
	if (card_fd == -1) {
		printf("open /dev/dri/card0: %s\n", STRERR);
		return -1;
	}

	res = alloc_mode_card_res(card_fd);
	if (!res) {
		printf("unable to create drm structure\n");
		ret = -1;
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
			ret = -1;
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
free_ret:
	free_connector(conn);
	free_mode_card_res(res);
	close(card_fd);
	return ret;
}

int read_args(int argc, char *argv[])
{
	int i;
	if (argc <= 1)
		return 0;
	for (i = 1; i < argc; ++i)
	{
		if (strncmp("--printmodes", argv[i], 13) == 0) {
			print_modes();
			_exit(0);
			return -1;
		}
		else {
			printf("unknown argument\n");
			return -1;
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct simple_drmbuffer *sfb = NULL;
	struct drm_mode_card_res *res = NULL;
	struct drm_mode_get_connector *conn = NULL;
	struct drm_mode_modeinfo *modes = NULL;
	uint32_t mode_count;
	uint32_t conn_id;
	int card_fd;
	int ret = 0;
	uint32_t i;
	int tty;
	int idx = -1;
	g_running = 1;
	memset(&g_state, 0, sizeof(g_state));
	memset(&g_server, 0, sizeof(g_server));
	g_server.vscroll_amount = 2;

	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	if (read_environ())
		return -1;
	if (read_args(argc, argv))
		return -1;

	card_fd = open("/dev/dri/card0", O_RDWR|O_CLOEXEC);
	if (card_fd == -1) {
		printf("open /dev/dri/card0: %s\n", STRERR);
		return -1;
	}
	if (card_set_master(card_fd)) {
		return -1;
	}

	res = alloc_mode_card_res(card_fd);
	if (!res) {
		printf("unable to create drm structure\n");
		ret = -1;
		goto free_ret;
	}

	i = 0;
	mode_count = 0;
	conn_id = drm_get_id(res->connector_id_ptr, i);
	conn = alloc_connector(card_fd, conn_id);
	if (!conn) {
		printf("unable to create drm structure\n");
		ret = -1;
		goto free_ret;
	}

	modes = get_connector_modeinfo(conn, &mode_count);
	idx = get_mode_idx(modes, mode_count,
			g_server.request_width,
			g_server.request_height,
			g_server.request_refresh);
	if (idx < 0)
		goto free_ret;

	printf("using mode[%d] (%dx%d@%dhz)\n",	idx,
			modes[idx].hdisplay,
			modes[idx].vdisplay,
			modes[idx].vrefresh);

	/* buffer pitch must divide evenly by 16,
	 * check against bpp here when that is variable */
	sfb = alloc_sfb(card_fd, modes[idx].hdisplay, modes[idx].vdisplay, 24, 32);
	if (!sfb) {
		ret = -1;
		goto free_ret;
	}

	if (connect_sfb(card_fd, conn, &modes[idx], sfb) == -1) {
		ret = -1;
		goto free_ret;
	}

	g_state.card_fd = card_fd;
	g_state.conn    = conn;
	g_state.sfb     = sfb;
	g_state.conn_modeidx = idx;
	conn = NULL;
	/* drmstate must be set before any SIGUSR1/2's might be sent! */
	drm_set_state(&g_state);

	tty = 0;
	/*K_XLATE, or K_MEDIUMRAW for keycodes, RAW is 8 bits*/
	if (vt_init(tty, K_XLATE)) {
		goto free_ret;
	}
	if (atexit(vt_shutdown)) {
		goto free_ret;
	}
	sig_setup();
	ret = exec_loop(tty);
	destroy_sfb(card_fd, sfb);
free_ret:
	/* TODO general cleanup function that loops through resources on a card */
	/* free_sfb !!! */
	free_connector(conn);
	free_mode_card_res(res);
	close(card_fd);
	return ret;

}


