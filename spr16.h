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

#ifndef SPR16_H__
#define SPR16_H__

#include <stdint.h>
#include <stddef.h>
/*
 * currently, one client = one shared mem region
 * memory is mapped as part of connection handshake.
 * resizing is not supported yet.
 *
 * for now, registered buffers cannot be resized, a pain-free implementation
 * of resizing would assume every resizable client just has full screen size
 *
 * this is not suitible for networked usage as-is, due to the nature of
 * struct padding on various architectures. you would want to add some sync
 * compression in addition to proper serialization if taking that route.
 *
 * NOTE: all structs must be evenly sized / divide evenly by 2
 * FIXME currently only supports 32bpp ARGB
 * TODO set sticky bit and check that, + perms on socket
 */

#define SPR16_MAXMSGLEN 64 /* hdr+data */
#define SPR16_MAXNAME   32
#define SPR16_MAX_SOCKET 128
#define SPR16_DEFAULT_SOCKET "socket"
#define SPR16_SOCKPATH "/tmp/spr16"
#define SPR16_ACK  1
#define SPR16_NACK 0
#define SPR16_MAXCLIENTS 128
#define SPR16_DMG_SLOTS 32

/* added precision for acceleration curve, 1 hardware unit == 10 spr16
 * don't change this, it should be safe to assume this is universally 10
 */
#define SPR16_RELATIVE_SCALE 10




/* msg types
 *
 * SERVINFO        - Server sends global parameters to client.
 * REGISTER_SPRITE - This message is handled only once to complete handshake.
 * 		     maps shared memory between server and client.
 * ACK             - ACK or NACK message.
 * SYNC            - Sync modified sprite region.
 * INPUT           - Send input event to client.
 */
enum {
	SPRITEMSG_SERVINFO=100,
	SPRITEMSG_REGISTER_SPRITE,
	SPRITEMSG_RECV_FD,
	SPRITEMSG_SEND_FD,
	SPRITEMSG_ACK,
	SPRITEMSG_SYNC,
	SPRITEMSG_INPUT,
	SPRITEMSG_INPUT_SURFACE
};

/* ack info
 *
 * RECV_FD	   - Tell receiver to prepare for receiving an fd, we set a flag
 *                   immediately after this message is sent so no other messages
 *                   should be exchanged until client responds with SEND_FD.
 * SEND_FD	   - Response for RECV_FD, notifies sender we cleared the message
 * 		     buffer and are ready for the file now. there is no message
 * 		     for the fd itself, just a single byte 'F' with ancillary data
 */
enum {
	SPRITEACK_ESTABLISHED=1,
	SPRITEACK_SYNC_VSYNC,
	SPRITEACK_SYNC_PAGEFLIP,
	SPRITEACK_RECV_FD,
	SPRITEACK_SEND_FD,
	SPRITENACK_SIZE, /* size might need to be padded beyond w*h*bpp */
	SPRITENACK_WIDTH,
	SPRITENACK_HEIGHT,
	SPRITENACK_BPP,
	SPRITENACK_SHMEM,
	SPRITENACK_FD,
	SPRITENACK_DISCONNECT
};

struct spr16_shmem {
	char *addr;
	int fd;
	uint32_t size;
};

#define SPRITE_FLAG_DIRECT_SHM 0x0001 /* client renders directly to sprite */
/* sprite object */
struct spr16 {
	char name[SPR16_MAXNAME];
	struct spr16_shmem shmem;
	uint32_t flags;
	int16_t  x;
	int16_t  y;
	int16_t  z;
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
};

/* msghdr bit flags/values */
#define SPRITESYNC_FLAG_ASYNC          0x0001
#define SPRITESYNC_FLAG_VBLANK         0x0002
#define SPRITESYNC_FLAG_PAGE_FLIP      0x0004 /* TODO */
#define SPRITESYNC_FLAG_MASK (	SPRITESYNC_FLAG_ASYNC     | \
				SPRITESYNC_FLAG_VBLANK    | \
				SPRITESYNC_FLAG_PAGE_FLIP )
/*
 * the msghdr is immediately followed by specific msgdata struct
 * these two structs should be written in the same write call
 * with no possibility of truncated writes.
 */
struct spr16_msghdr {
	uint16_t type;
	uint16_t bits;
	/* if you add to this be warned, linux platform code assumes this struct is only
	 * 2 uint16_t's, in a few places. make sure you know what you're doing
	 */
};

struct spr16_msgdata_servinfo {
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
};

/* this message is immediately followed by 1 byte SCM_RIGHTS message
 * to transfer sprite file descriptor to server process */
struct spr16_msgdata_register_sprite {
	char name[SPR16_MAXNAME];
	uint32_t flags;
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
};

/* can be ack or nack, with some info */
struct spr16_msgdata_ack {
	uint16_t info;
	uint16_t ack; /* 0 == nack */
};

/* client requesting synchronization */
struct spr16_msgdata_sync {
	uint16_t xmin;
	uint16_t ymin;
	uint16_t xmax;
	uint16_t ymax;
};

/*
 * type values:
 *      key      - code=key  val=state
 * 	absolute - code=axis val=pos   ext=max
 * 	surface  - code=id   val=mag   ext=magmax
 */
struct spr16_msgdata_input {
	int32_t  val;
	int32_t  ext;
	uint16_t code;
	uint8_t  type;
	uint8_t  id; /* device id */
};

struct spr16_msgdata_input_surface {
	struct spr16_msgdata_input input;
	int32_t xpos;
	int32_t ypos;
	int32_t xmax;
	int32_t ymax;
};

enum {
	SPR16_INPUT_CONTROL = 1,   /* control codes */
	SPR16_INPUT_KEY,       /* full spr16 keycodes */
	SPR16_INPUT_KEY_ASCII,
	SPR16_INPUT_AXIS_RELATIVE,
	SPR16_INPUT_AXIS_ABSOLUTE,
	SPR16_INPUT_SURFACE
};

#define SPR16_SURFACE_MAX_CONTACTS 64
enum {
	SPR16_CTRLCODE_RESET = 0 /* clients that track input state should reset */
};

enum {
	/* 0-255 */
	SPR16_KEYCODE_ESCAPE = 27,
	/* beginning of non-ascii values, everything above here is just normal
	 * ascii codes, add defines as needed like already done with escape. */
	SPR16_KEYCODE_LSHIFT = 0x0100,
	SPR16_KEYCODE_RSHIFT,
	SPR16_KEYCODE_LCTRL,
	SPR16_KEYCODE_RCTRL,
	SPR16_KEYCODE_LALT,
	SPR16_KEYCODE_RALT,
	SPR16_KEYCODE_UP,
	SPR16_KEYCODE_CAPSLOCK,
	SPR16_KEYCODE_DOWN,
	SPR16_KEYCODE_LEFT,
	SPR16_KEYCODE_RIGHT,
	SPR16_KEYCODE_PAGEUP,
	SPR16_KEYCODE_PAGEDOWN,
	SPR16_KEYCODE_HOME,
	SPR16_KEYCODE_END,
	SPR16_KEYCODE_INSERT,
	SPR16_KEYCODE_DELETE,
	SPR16_KEYCODE_NUMLOCK,
	SPR16_KEYCODE_NP0,
	SPR16_KEYCODE_NP1,
	SPR16_KEYCODE_NP2,
	SPR16_KEYCODE_NP3,
	SPR16_KEYCODE_NP4,
	SPR16_KEYCODE_NP5,
	SPR16_KEYCODE_NP6,
	SPR16_KEYCODE_NP7,
	SPR16_KEYCODE_NP8,
	SPR16_KEYCODE_NP9,
	SPR16_KEYCODE_NPSLASH,
	SPR16_KEYCODE_NPASTERISK,
	SPR16_KEYCODE_NPPLUS,
	SPR16_KEYCODE_NPMINUS,
	SPR16_KEYCODE_NPDOT,
	SPR16_KEYCODE_NPENTER,
	SPR16_KEYCODE_F1,
	SPR16_KEYCODE_F2,
	SPR16_KEYCODE_F3,
	SPR16_KEYCODE_F4,
	SPR16_KEYCODE_F5,
	SPR16_KEYCODE_F6,
	SPR16_KEYCODE_F7,
	SPR16_KEYCODE_F8,
	SPR16_KEYCODE_F9,
	SPR16_KEYCODE_F10,
	SPR16_KEYCODE_F11,
	SPR16_KEYCODE_F12,
	SPR16_KEYCODE_F13,
	SPR16_KEYCODE_F14,
	SPR16_KEYCODE_F15,
	SPR16_KEYCODE_F16,
	SPR16_KEYCODE_F17,
	SPR16_KEYCODE_F18,
	SPR16_KEYCODE_F19,
	SPR16_KEYCODE_F20,
	SPR16_KEYCODE_F21,
	SPR16_KEYCODE_F22,
	SPR16_KEYCODE_F23,
	SPR16_KEYCODE_F24,
	SPR16_KEYCODE_ABTN,
	SPR16_KEYCODE_BBTN,
	SPR16_KEYCODE_CBTN,
	SPR16_KEYCODE_DBTN,
	SPR16_KEYCODE_EBTN,
	SPR16_KEYCODE_FBTN,
	SPR16_KEYCODE_LBTN,
	SPR16_KEYCODE_RBTN,
	SPR16_KEYCODE_SBTN,
	SPR16_KEYCODE_CONTACT, /* surface contact */
	SPR16_KEYCODE_COUNT
};

typedef int (*input_handler)(struct spr16_msgdata_input *input);
typedef int (*input_surface_handler)(struct spr16_msgdata_input_surface *surface);
typedef int (*servinfo_handler)(struct spr16_msgdata_servinfo *sinfo);

/*----------------------------------------------*
 * message handling                             *
 *----------------------------------------------*/
struct client;
uint32_t get_msghdr_typelen(struct spr16_msghdr *hdr);
struct spr16_msgdata_servinfo *spr16_get_servinfo_msg(int fd, uint32_t *outlen, int timeout);
char *spr16_read_msgs(int fd, uint32_t *outlen);
int spr16_write_msg(int fd, struct spr16_msghdr *hdr, void *msgdata, size_t msgdata_len);
int spr16_send_ack(int fd, uint16_t ackinfo);
int spr16_send_nack(int fd, uint16_t ackinfo);
int afunix_send_fd(int sock, int fd);
int afunix_recv_fd(int sock, int *fd_out);


/*----------------------------------------------*
 * client side                                  *
 *----------------------------------------------*/
int spr16_client_init();
int spr16_client_connect(char *name);
int spr16_client_handshake_start(char *name, uint16_t width, uint16_t height, uint32_t flags);
int spr16_client_handshake_wait(uint32_t timeout);
int spr16_client_servinfo(struct spr16_msgdata_servinfo *sinfo);
int spr16_client_register_sprite(char *name, uint16_t width, uint16_t height, uint32_t flags);
int spr16_client_update(int poll_timeout); /* timeout in milliseconds, <0 blocks */
int spr16_client_shutdown();
struct spr16_msgdata_servinfo *spr16_client_get_servinfo();
int spr16_client_ack(struct spr16_msgdata_ack *ack);
struct spr16 *spr16_client_get_sprite();
/* may return -1 with errno set to EAGAIN, if server buffer is full */
int spr16_client_sync(uint16_t xmin, uint16_t ymin,
		      uint16_t xmax, uint16_t ymax, uint16_t flags);
int spr16_client_waiting_for_vsync();
int spr16_client_input(struct spr16_msgdata_input *msg);
int spr16_client_input_surface(struct spr16_msgdata_input_surface *msg);
int spr16_client_set_servinfo_handler(servinfo_handler func);

/* input callbacks */
int spr16_client_set_input_handler(input_handler func);
int spr16_client_set_input_surface_handler(input_surface_handler func);


/*----------------------------------------------*
 * server side                                  *
 *----------------------------------------------*/
struct spr16_framebuffer
{
	char    *addr;
	size_t   size;
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
	/*uint16_t depth;*/
};

struct server_options
{
	/* TODO sweep up common global clutter here */
	char socket_name[SPR16_MAX_SOCKET];
	uint16_t request_width;
	uint16_t request_height;
	uint16_t request_refresh;
	uint32_t tap_delay; /* surface tap click delay */
	int pointer_accel;
	int vscroll_amount;
	int inactive_vt;
};

struct client
{
	struct spr16 sprite;
	struct spr16_msgdata_sync dmg[SPR16_DMG_SLOTS];
	struct client *next;
	uint16_t dmg_count;
	uint32_t sync_flags;
	int syncing;
	int handshaking;
	int connected; /* set nonzero after handshake */
	int recv_fd_wait;
	int socket;
};

/* TODO move this into framebuffer struct */
int  spr16_server_is_active();
void spr16_server_activate();
void spr16_server_deactivate();

/*int  spr16_server_servinfo(int fd);
int  spr16_server_sync(struct client *cl, uint16_t flags, struct spr16_msgdata_sync *region);
int  spr16_server_register_sprite(int fd, struct spr16_msgdata_register_sprite *reg);
int  spr16_open_memfd(struct spr16_msgdata_register_sprite *reg);
*/


/*
 * FIXME -- this has changed
 * Simple usage scenario:
 *
 * client                              server
 *   |                                    |
 *   |                                    init(tty1)
 *   connect -------------------------->  |
 *   |  <-------------------------------- servinfo
 *   register_buffer ------------------>  |
 *   send_descriptor ------------------>  |
 *   |  <-------------------------------- ack(ESTABLISHED)/nack()
 *   sync_region(id,x,y,w,h) ---------->  |
 *   sync_region(id,x,y,w,h) ---------->  |
 *   |  <-------------------------------- nack
 *   X
 */



/*----------------------------------------------*
 * input drivers                                *
 *----------------------------------------------*/

/* number of longs needed to represent n bits. */
#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(n) ((n+LONG_BITS-1)/LONG_BITS)

/* server callback for hotkey events */
struct input_device;
enum {
	SPR16_HOTKEY_AXE = 1,
	SPR16_HOTKEY_NEXTSCREEN,
	SPR16_HOTKEY_PREVSCREEN
};
typedef int (*input_hotkey)(uint32_t hk, void *v);
typedef int (*input_transceive)(struct input_device *self, int client);
typedef int (*input_flush)(struct input_device *self);
struct input_device {
	unsigned long btn_down[NLONGS(SPR16_KEYCODE_COUNT)];
	char path[128];
	char name[64];
	input_flush func_flush;
	input_hotkey func_hotkey;
	/*input_transceive func_transceive;*/
	void *private; /* first variable always uint32_t type */
	void *srv_ctx; /* server context for callbacks */
	struct input_device *next;
	uint32_t keyflags;
	uint8_t device_id;
	int fd;
};

#endif
