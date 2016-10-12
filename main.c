/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * gtscreen - graphic type screen
 *
 */

/*
 *  TODO: evdev support!!!
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

	listen_socket = spr16_server_init();
	if (listen_socket == -1 ){
		printf("server init failed\n");
		return -1;
	}
	if (spr16_server_init_input(tty)) {
		printf("input init failed\n");
		return -1;
	}
	while (g_running)
	{
		if (spr16_server_update(listen_socket)) {
			printf("spr16 update error\n");
			return -1;
		}
	}
	return 0;
}

int main()
{
	struct simple_drmbuffer *sfb = NULL;
	struct drm_mode_card_res *res = NULL;
	struct drm_mode_get_connector *conn = NULL;
	struct drm_mode_modeinfo *modes = NULL;
	int card_fd;
	int ret = 0;
	uint32_t i;
	int tty;

	g_running = 1;
	memset(&g_state, 0, sizeof(g_state));

	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

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

	/*printf("--- Card 0 Resources ----------------------\n");
	print_mode_card_resources(res);*/
	/* print connector info */
	/*for (i = 0; i < res->count_connectors; ++i)*/
	i = 0;
	{
		uint32_t mode_count = 0;
		uint32_t conn_id = drm_get_id(res->connector_id_ptr, i);
		conn = alloc_connector(card_fd, conn_id);
		if (!conn) {
			printf("unable to create drm structure\n");
			ret = -1;
			goto free_ret;
		}
		/*printf("--- Connector %d --------------------------\n", conn_id);
		print_connector(conn);*/

		printf("creating simple framebuffer\n");
		/* choose a mode TODO make a mode hint picker thingy */
		modes = get_connector_modeinfo(conn, &mode_count);
		sfb = alloc_sfb(card_fd, modes[0].hdisplay, modes[0].vdisplay, 24, 32);
		if (!sfb) {
			ret = -1;
			goto free_ret;
		}

		if (connect_sfb(card_fd, conn, &modes[0], sfb) == -1) {
			ret = -1;
			/*free_connector(conn); */
			goto free_ret;
		}
	}

	g_state.card_fd = card_fd;
	g_state.conn    = conn;
	g_state.sfb     = sfb;
	g_state.conn_modeidx = 0;
	/* drmstate must be set before any SIGUSR1/2's might be sent! */
	drm_set_state(&g_state);
	/*if (card_drop_master(card_fd)) {
		ret = -1;
		goto free_ret;
	}*/
	tty = 0;
	if (vt_init(tty, K_XLATE)) {
		return -1;
	}
	if (atexit(vt_shutdown)) {
		return -1;
	}
	sig_setup();
	ret = exec_loop(tty);
	destroy_sfb(card_fd, sfb);
free_ret:
	/* TODO general cleanup function that loops through resources on a card */
	/* free_sfb !!! */
	free_connector(g_state.conn);
	free_mode_card_res(res);
	close(card_fd);
	return ret;

}


