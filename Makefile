#########################################
# global defines
#########################################
ifndef DESTDIR
DESTDIR=/usr/local
endif
CFGDIR:="$(DESTDIR)/etc/gtscreen"

# if you use a 64 bit system set bitcount(TODO untested).
# errgh, and fix the memfd syscall it only works on x86
# because syscall numbers are not the same...
DEFINES := -DMAX_SYSTEMPATH=1024 -DPTRBITCOUNT=32
CFLAGS  := -pedantic -Wall -Wextra -Werror $(DEFINES)
DEFLANG := -ansi

#DBG	:= -g
#DBG_LDFLAGS := -rdynamic

#########################################
# objects
#########################################
GTSCREEN_SRCS := ./main.c				\
		 ./screen.c				\
		 ./platform/linux/drm.c			\
		 ./platform/linux/vt.c			\
		 ./platform/linux/fb.c			\
		 ./platform/linux/input.c		\
		 ./platform/linux/messages.c		\
		 ./platform/linux/server.c		\
		 ./platform/fdpoll-handler.c

GTSCREEN_OBJS := $(GTSCREEN_SRCS:.c=.gtscreen.o) \
		 ./platform/x86.asm.o
# example program
LANDIT_SRCS := 	./examples/landit.c		\
		./examples/game.c		\
		./examples/util.c		\
		./examples/dynamics.c		\
		./examples/moon.c		\
		./examples/craft.c		\
		./examples/particle.c		\
		./platform/linux/messages.c	\
		./platform/linux/client.c	\
		./platform/linux/drm.c
LANDIT_OBJS := $(LANDIT_SRCS:.c=.spr16_ex.o)

# touch input example
TOUCHPAINT_SRCS := ./examples/touchpaint.c		\
		   ./examples/util.c			\
		   ./platform/linux/messages.c		\
		   ./platform/linux/client.c		\
		   ./platform/linux/drm.c
TOUCHPAINT_OBJS := $(TOUCHPAINT_SRCS:.c=.touchpaint.o)

# screen tearing test pattern
VSYNC_TEST_SRCS := ./examples/vsync-test.c		\
		   ./examples/util.c			\
		   ./platform/linux/messages.c		\
		   ./platform/linux/client.c		\
		   ./platform/linux/drm.c
VSYNC_TEST_OBJS := $(VSYNC_TEST_SRCS:.c=.vsync-test.o)

#  spr16-x11-xorg graphic drivers
SPORG_GFX_SRCS := 	./airlock/sporg/sporg.c		\
			./airlock/sporg/sporg_client.c	\
			./platform/linux/messages.c	\
			./platform/linux/client.c	\
			./platform/linux/drm.c
SPORG_GFX_OBJS := $(SPORG_GFX_SRCS:.c=.sporg_gfx.o)
SPORG_GFX_INC := -I/usr/include/xorg -I/usr/include/pixman-1

#  spr16-x11-xorg input driver
SPORG_INPUT_SRCS := ./airlock/sporg/sporg_input.c
SPORG_INPUT_OBJS := $(SPORG_INPUT_SRCS:.c=.sporg_input.o)
SPORG_INPUT_INC := -I/usr/include/xorg -I/usr/include/pixman-1

########################################
# target files
########################################
GTSCREEN    := gtscreen
LANDIT      := landit
TOUCHPAINT  := touchpaint
VSYNC_TEST  := vsync_test
SPORG_GFX   := sporg_drv.so
SPORG_INPUT := sporginput_drv.so


########################################
# build
########################################
%.asm.o: %.s
	$(AS) -o $@ $<
%.gtscreen.o: %.c
	$(CC) -c $(DEFLANG) -DSPR16_SERVER $(CFLAGS) $(DBG) -o $@ $<
%.spr16_ex.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.touchpaint.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.vsync-test.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<

%.sporg_gfx.o: %.c
	$(CC) -c -std=gnu99 -pedantic -Wall -fPIC $(DBG) $(SPORG_GFX_INC) -o $@ $<

%.sporg_input.o: %.c
	$(CC) -c -std=gnu99 -pedantic -Wall -fPIC $(DBG) $(SPORG_INPUT_INC) -o $@ $<
all:			\
	$(GTSCREEN)	\
	$(LANDIT)	\
	$(TOUCHPAINT)	\
	$(VSYNC_TEST)	\
	$(SPORG_GFX)	\
	$(SPORG_INPUT)

########################################
# targets
########################################

# -lm for floating point is only by gtscreen to calculate relative acceleration
# and to convert absoulute touch devices to relative trackpads.
# this can be ifdef'd out or done using some integer technique for systems without fpu
# TODO also used by examples/util.c for vector math, which could be in it's own C file
$(GTSCREEN):		$(GTSCREEN_OBJS)
			$(CC) $(DEFINES) -lm $(LDFLAGS) $(DBG_LDFLAGS) $(GTSCREEN_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| gtscreen         OK |"
			@echo "x---------------------x"
			@echo ""

$(LANDIT):		$(LANDIT_OBJS)
			$(CC) $(LDFLAGS) -lm $(LANDIT_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| landit           OK |"
			@echo "x---------------------x"
			@echo ""

$(TOUCHPAINT):		$(TOUCHPAINT_OBJS)
			$(CC) $(LDFLAGS) -lm $(TOUCHPAINT_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| touchpaint       OK |"
			@echo "x---------------------x"
			@echo ""

$(VSYNC_TEST):		$(VSYNC_TEST_OBJS)
			$(CC) $(LDFLAGS) -lm $(VSYNC_TEST_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| vsync_test       OK |"
			@echo "x---------------------x"
			@echo ""

$(SPORG_GFX):		$(SPORG_GFX_OBJS)
			$(CC) $(LDFLAGS) -shared $(SPORG_GFX_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| sporg_gfx        OK |"
			@echo "x---------------------x"
			@echo ""

$(SPORG_INPUT):	$(SPORG_INPUT_OBJS)
			$(CC) $(LDFLAGS) -shared -lX11 $(SPORG_INPUT_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| sporg_input      OK |"
			@echo "x---------------------x"
			@echo ""

install:
	@umask 022
	@install -dvm 0755  "$(DESTDIR)/bin"
	@install -dvm 0755  "$(DESTDIR)/lib/xorg/modules/drivers"
	@install -dvm 0755  "$(DESTDIR)/lib/xorg/modules/input"
	@install -dvm 0755  "$(CFGDIR)"
	@install -Dvm 0755  "$(GTSCREEN)"    "$(DESTDIR)/bin/$(GTSCREEN)"
	@install -Dvm 0755  "$(TOUCHPAINT)"  "$(DESTDIR)/bin/$(TOUCHPAINT)"
	@install -Dvm 0755  "$(VSYNC_TEST)"  "$(DESTDIR)/bin/$(VSYNC_TEST)"
	@install -Dvm 0755  "$(LANDIT)"      "$(DESTDIR)/bin/$(LANDIT)"
	@install -Dvm 0755  airlock/sporg/xorg.conf "$(CFGDIR)"
	@install -Dvm 0755  "$(SPORG_GFX)"   \
		"$(DESTDIR)/lib/xorg/modules/drivers/$(SPORG_GFX)"
	@install -Dvm 0755  "$(SPORG_INPUT)" \
		"$(DESTDIR)/lib/xorg/modules/input/$(SPORG_INPUT)"

clean:
	@$(foreach obj, $(GTSCREEN_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(LANDIT_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(TOUCHPAINT_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(VSYNC_TEST_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPORG_GFX_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPORG_INPUT_OBJS), rm -fv $(obj);)

	@-rm -fv ./$(GTSCREEN)
	@-rm -fv ./$(LANDIT)
	@-rm -fv ./$(TOUCHPAINT)
	@-rm -fv ./$(VSYNC_TEST)
	@-rm -fv ./$(SPORG_GFX)
	@-rm -fv ./$(SPORG_INPUT)
	@echo "cleaned."

