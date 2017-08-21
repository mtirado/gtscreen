/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */

#ifndef SPR16_H__
#define SPR16_H__

#include <stdint.h>
#include <stddef.h>
/*
 * This is a suuuuper simple display protocol.
 * buffer id=0 should be invalid,
 *
 * one client = one shared mem region, that's it!
 * memory is mapped as part of connection handshake.
 * resizing is not supported.
 *
 * TODO currently it assumes 32bpp ARGB
 *
 * for simplicity sake, registered buffers cannot be shrunk or increased.
 * this may be possible eventually but would require a buffer remap, or sync
 * over pipes instead of shmem. there may be platform specific options to
 * improve this, but i have not gone through DRM thoroughly enough to get
 * a clear picture of how the protocol would look while still supporting
 * complete software-only operation, so this protocol is obviously
 * not set in stone..
 *
 * this is not suitible for networked usage, due to the nature of
 * struct padding on various architectures. you would want to add some sync
 * compression in addition to proper serialization if taking that route.
 * NOTE: all structs must be evenly sized / divide evenly by 2, unless you
 * want to handle adding a triple read if msg truncates with 1 byte fragment
 * for some crazy reason, i don't know if all arch's are safe from this?
 */


#define SPR16_MAXMSGLEN 64 /* hdr+data */
#define SPR16_MAXNAME   32
#define SPR16_SOCKPATH "/tmp/spr16"
#define SPR16_LOGPATH "/usr/var/log/spr16"
#define SPR16_ACK  1
#define SPR16_NACK 0
#define SPR16_MAXCLIENTS 128


/* added precision for acceleration curve, 1 hardware unit == 10 spr16
 * don't change this, it should be safe to assume this is universally 10
 */
#define SPR16_RELATIVE_SCALE 10


/* TODO set sticky bit and check that, + perms like xephyr does ;)
 *
 * */


/* ack info */
enum {
	SPRITEACK_SEND_DESCRIPTOR=1,
	SPRITEACK_ESTABLISHED,
	SPRITENACK_SIZE, /* size might need to be padded beyond w*h*bpp */
	SPRITENACK_WIDTH,
	SPRITENACK_HEIGHT,
	SPRITENACK_BPP,
	SPRITENACK_SHMEM,
	SPRITENACK_DISCONNECT
};

/* msg types
 *
 * SERVINFO        - Server sends global parameters to client.
 * REGISTER_SPRITE - This message is handled only once to complete handshake.
 * 		     maps shared memory between server and client.
 * ACK             - ACK or NACK message.
 * SYNC            - Sync modified sprite region.
 * INPUT           - Send input event to client.
 *
 * */
enum {
	SPRITEMSG_SERVINFO=100,
	SPRITEMSG_REGISTER_SPRITE,
	SPRITEMSG_ACK,
	SPRITEMSG_SYNC,
	SPRITEMSG_INPUT,
	SPRITEMSG_INPUT_SURFACE
};

/* bit flags
 * TODO so clients don't have to keep rendering if invisible
 */
#define SPRITE_FLAG_VISIBLE  0x01
#define SPRITE_FLAG_INVERT_Y 0x02

struct spr16_shmem {
	char *addr;
	int fd;
	uint32_t size;
};

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
	/* TODO surface formats, just using drm default 32bit ARGB */
};

/* can be ack or nack, with some info */
struct spr16_msgdata_ack {
	uint16_t info;
	uint16_t ack; /* 0 == nack */
};

/* client requesting synchronization */
struct spr16_msgdata_sync {
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;
};

/*
 * TODO stop using evdev code defines.
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
	SPR16_INPUT_KEY = 1,   /* full spr16 keycodes */
	SPR16_INPUT_KEY_ASCII, /* fallback ascii mapped keycodes */
	SPR16_INPUT_AXIS_RELATIVE,
	SPR16_INPUT_AXIS_ABSOLUTE,
	SPR16_INPUT_SURFACE
};

#define SPR16_SURFACE_MAX_CONTACTS 64



/* TODO some vt switch other than linux kernel vt keyboard */
#define SPR16_KEYMOD_LALT  0x00000001
#define SPR16_KEYMOD_LCTRL 0x00000002

enum {
	SPR16_KEYCODE_ESCAPE = 27,
	SPR16_KEYCODE_LSHIFT = 0x0100, /* beginning of non-ascii values */
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
	SPR16_KEYCODE_LBTN,
	SPR16_KEYCODE_RBTN,
	SPR16_KEYCODE_ABTN,
	SPR16_KEYCODE_BBTN,
	SPR16_KEYCODE_CBTN,
	SPR16_KEYCODE_DBTN,
	SPR16_KEYCODE_EBTN,
	SPR16_KEYCODE_FBTN,
	SPR16_KEYCODE_SBTN,
	SPR16_KEYCODE_CONTACT
};

typedef int (*input_handler)(struct spr16_msgdata_input *input);
typedef int (*input_surface_handler)(struct spr16_msgdata_input_surface *surface);
typedef int (*servinfo_handler)(struct spr16_msgdata_servinfo *sinfo);

/*----------------------------------------------*
 * message handling                             *
 *----------------------------------------------*/
char *spr16_read_msgs(int fd, uint32_t *outlen);
int spr16_dispatch_msgs(int fd, char *msgbuf, uint32_t buflen);
int spr16_write_msg(int fd, struct spr16_msghdr *hdr,
                    void *msgdata, size_t msgdata_len);
int spr16_send_ack(int fd, uint16_t ack, uint16_t ackinfo);
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
/* TODO pixel formats */
int spr16_client_register_sprite(char *name, uint16_t width, uint16_t height, uint32_t flags);
int spr16_client_update(int poll_timeout); /* timeout in milliseconds, <0 blocks */
int spr16_client_shutdown();
struct spr16_msgdata_servinfo *spr16_client_get_servinfo();
int spr16_client_ack(struct spr16_msgdata_ack *ack);
struct spr16 *spr16_client_get_sprite();
/* may return -1 with errno set to EAGAIN, if server buffer is full */
int spr16_client_sync(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
int spr16_client_input(struct spr16_msgdata_input *msg);
int spr16_client_input_surface(struct spr16_msgdata_input_surface *msg);
int spr16_client_set_servinfo_handler(servinfo_handler func);

/* input callbacks */
int spr16_client_set_input_handler(input_handler func);
int spr16_client_set_input_surface_handler(input_surface_handler func);


/*----------------------------------------------*
 * server side                                  *
 *----------------------------------------------*/
struct server
{
	/* TODO sweep up common global clutter here */
	uint16_t request_width;
	uint16_t request_height;
	uint16_t request_refresh;
	int pointer_accel;
	int vscroll_amount;
};
struct client
{
	struct spr16 sprite;
	struct client *next;
	int handshaking;
	int connected; /* set nonzero after handshake */
	int socket;
};
int spr16_server_init(uint16_t width, uint16_t height, uint16_t bpp);
int spr16_server_servinfo(int fd);
int spr16_server_sync(struct spr16_msgdata_sync *region);
int spr16_server_register_sprite(int fd, struct spr16_msgdata_register_sprite *reg);
int spr16_server_update();
int spr16_open_memfd(struct spr16_msgdata_register_sprite *reg);
int spr16_server_init_input();
int spr16_server_shutdown(int listen_fd);



/*
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
/* server callback for hotkey events */
struct input_device;
enum {
	SPR16_HOTKEY_NEXTSCREEN = 1,
	SPR16_HOTKEY_PREVSCREEN
};
typedef int (*input_hotkey)(uint32_t hk, void *v);
typedef int (*input_transceive)(struct input_device *self, int client);
typedef int (*input_flush)(struct input_device *self);
struct input_device {
	char path[128];
	char name[64];
	input_flush func_flush;
	input_transceive func_transceive;
	input_hotkey func_hotkey;
	void *private; /* first variable always uint32_t type */
	struct input_device *next;
	uint32_t keyflags;
	uint8_t device_id;
	int fd;
};

struct screen {
	struct screen *next;
	struct client *clients;
	struct client *focused_client;
};
#endif
