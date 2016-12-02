/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */

#define _GNU_SOURCE
#include "../defines.h"
#include "linux-vt.h"
#include "linux-drm.h"
#include <linux/vt.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

#define STRERR strerror(errno)

int g_kbmode;
int g_ttyfd;
struct termios g_origterm;

void vt_sig(int signum)
{
	switch (signum)
	{
	case SIGUSR1:
		/* vt is switching on */
		if (ioctl(g_ttyfd, VT_RELDISP, VT_ACKACQ) == -1) {
			printf("ioctl(VT_RELDISP, VT_ACKACQ): %s\n", STRERR);
		}
		drm_acquire_signal();
		break;
	case SIGUSR2:
		/* vt is switchign off */
		drm_release_signal();
		if (ioctl(g_ttyfd, VT_RELDISP, 1) == -1) {
			printf("ioctl(VT_RELDISP, 1): %s\n", STRERR);
		}
		break;
	default:
		break;
	}
}

static void vt_sig_setup()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = vt_sig;
	sigaction(SIGUSR1, &sa, NULL);
	sa.sa_handler = vt_sig;
	sigaction(SIGUSR2, &sa, NULL);
}

static void vt_setmode_textauto()
{
	struct vt_mode vtm;
	memset(&vtm, 0, sizeof(vtm));
	vtm.mode = VT_AUTO;

	if (ioctl(g_ttyfd, KDSETMODE, KD_TEXT) == -1)
		printf("ioctl(KDSETMODE, KD_TEXT): %s\n", STRERR);
	if (ioctl(g_ttyfd, VT_SETMODE, &vtm) == -1)
		printf("ioctl(VT_SETMODE): %s\n", STRERR);

}

void vt_shutdown()
{
	if (ioctl(g_ttyfd, KDSKBMODE, g_kbmode) == -1)
		printf("ioctl(KDSKBMODE, K_OFF): %s\n", STRERR);
	vt_setmode_textauto();
	tcsetattr(g_ttyfd, TCSANOW, &g_origterm);
	tcflush(g_ttyfd, TCIOFLUSH);
}

int vt_init(int tty_fd, unsigned int kbd_mode)
{
	struct vt_mode vtm;
	struct termios tms;

	g_ttyfd = tty_fd;
	vt_sig_setup();

	if (ioctl(tty_fd, KDGKBMODE, &g_kbmode) == -1) {
		printf("ioctl(KDGKBMODE): %s\n", STRERR);
		vt_setmode_textauto();
		return -1;
	}

	if (ioctl(tty_fd, KDSKBMODE, kbd_mode) == -1) {
		printf("ioctl(KDSKBMODE, %d): %s\n", kbd_mode, STRERR);
		vt_setmode_textauto();
		return -1;
	}

	if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) == -1) {
		printf("ioctl(KDSETMODE, KD_GRAPHICS): %s\n", STRERR);
		vt_shutdown();
		return -1;
	}

	memset(&vtm, 0, sizeof(vtm));
	vtm.mode = VT_PROCESS;
	vtm.acqsig = SIGUSR1;
	vtm.relsig = SIGUSR2;

	if (ioctl(tty_fd, VT_SETMODE, &vtm) == -1) {
		printf("ioctl(VT_SETMODE): %s\n", STRERR);
		vt_shutdown();
		return -1;
	}
	/* set termios to raw mode */
	if (tcgetattr(tty_fd, &tms)) {
		vt_shutdown();
		return -1;
	}
	memcpy(&g_origterm, &tms, sizeof(struct termios));
	/* set terminal to raw mode */
	cfmakeraw(&tms);
	tms.c_lflag |= ISIG; /* get signals */

	tms.c_cflag |= CS8;

	/* disable controls chars */
	tms.c_cc[VDISCARD]  = _POSIX_VDISABLE;
	tms.c_cc[VEOF]	    = _POSIX_VDISABLE;
	tms.c_cc[VEOL]	    = _POSIX_VDISABLE;
	tms.c_cc[VEOL2]	    = _POSIX_VDISABLE;
	tms.c_cc[VERASE]    = _POSIX_VDISABLE;
	/* XXX
	 * tms.c_cc[VINTR]	    = _POSIX_VDISABLE;
	 */
	tms.c_cc[VKILL]	    = _POSIX_VDISABLE;
	tms.c_cc[VLNEXT]    = _POSIX_VDISABLE;
	tms.c_cc[VMIN]	    = 1;
	tms.c_cc[VQUIT]	    = _POSIX_VDISABLE;
	tms.c_cc[VREPRINT]  = _POSIX_VDISABLE;
	tms.c_cc[VSTART]    = _POSIX_VDISABLE;
	tms.c_cc[VSTOP]	    = _POSIX_VDISABLE;
	tms.c_cc[VSUSP]	    = _POSIX_VDISABLE;
	tms.c_cc[VSWTC]	    = _POSIX_VDISABLE;
	tms.c_cc[VTIME]	    = 0;
	tms.c_cc[VWERASE]   = _POSIX_VDISABLE;
	/* set it! */
	if (tcsetattr(tty_fd, TCSANOW, &tms)) {
		vt_shutdown();
		return -1;
	}
	tcflush(tty_fd, TCIOFLUSH);

	return 0;
}
