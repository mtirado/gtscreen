/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <memory.h>
#include <unistd.h>
#include "../protocol/spr16.h"
#include "game.h"

#define STRERR strerror(errno)


int main(int argc, char *argv[])
{
	struct spr16 *screen;
	int r;

	if (argc < 2) {
		printf("usage: program <server-name>\n");
		printf("e.g: spr16_example tty1\n");
		return -1;
	}
	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	r = spr16_client_connect(argv[1]);
	if (r == -1) {
		printf("connect(%s) error:\n", argv[1]);
		return -1;
	}

	spr16_client_set_input_handler(game_input);
	if (spr16_client_handshake_wait(10000)) {
		printf("handshake timed out\n");
		return -1;
	}
	screen = spr16_client_get_sprite();
	if (!screen)
		return -1;
	if (game_init(screen))
		return -1;
	while (1)
	{
		r = spr16_client_update();
		if (r == -1) {
			printf("spr16 update error\n");
			return -1;
		}
		if (game_update())
			return -1;
		if (game_draw())
			return -1;
		usleep(1000);
	}
	spr16_client_shutdown();

	return 0;
}



