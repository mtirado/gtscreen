#########################################
# global defines
#########################################
ifndef DESTDIR
DESTDIR=/usr/local
endif
ifndef CFGDIR
CFGDIR:="etc/gtscreen"
endif
ifndef BINDIR
BINDIR:="bin"
endif
ifndef INCDIR
INCDIR:="include/spr16"
endif
ifndef LIBDIR
LIBDIR:="lib"
endif
ifndef XORG_MOD_DIR
XORG_MOD_DIR:=lib/xorg/modules
endif

# if you use a 64 bit system set bitcount(TODO untested).
# errgh, and fix the memfd syscall it only works on x86
# because syscall numbers are not the same...
DEFINES := -DMAX_SYSTEMPATH=1024 -DPTRBITCOUNT=32 -DPIXL_ALIGN=32
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
		./examples/particle.c
LANDIT_OBJS := $(LANDIT_SRCS:.c=.spr16_ex.o)	\
		./lib/libspr16_cl.a

# touch input example
TOUCHPAINT_SRCS := ./examples/touchpaint.c		\
		   ./examples/util.c
TOUCHPAINT_OBJS := $(TOUCHPAINT_SRCS:.c=.touchpaint.o)	\
		   ./lib/libspr16_cl.a

# screen tearing test pattern
VSYNC_TEST_SRCS := ./examples/vsync-test.c		\
		   ./examples/util.c
VSYNC_TEST_OBJS := $(VSYNC_TEST_SRCS:.c=.vsync-test.o)	\
		   ./lib/libspr16_cl.a

#  spr16-x11-xorg graphic drivers
SPORG_GFX_SRCS := ./airlock/sporg/sporg.c		\
		  ./airlock/sporg/sporg_client.c
SPORG_GFX_OBJS := $(SPORG_GFX_SRCS:.c=.sporg_gfx.o)	\
		  ./lib/libspr16_cl.a
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
LIB_CLIENT  := libspr16_cl.a

########################################
# build
########################################
%.asm.o: %.s
	$(AS) -o $@ $<
%.c.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
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
	$(LIB_CLIENT)	\
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
			@echo "x----------------x"
			@echo "| gtscreen       |"
			@echo "x----------------x"
			@echo ""

$(LANDIT):		$(LANDIT_OBJS)
			$(CC) $(LDFLAGS) -lm $(LANDIT_OBJS) -o $@
			@echo ""
			@echo "x----------------x"
			@echo "| landit         |"
			@echo "x----------------x"
			@echo ""

$(TOUCHPAINT):		$(TOUCHPAINT_OBJS)
			$(CC) $(LDFLAGS) -lm $(TOUCHPAINT_OBJS) -o $@
			@echo ""
			@echo "x----------------x"
			@echo "| touchpaint     |"
			@echo "x----------------x"
			@echo ""

$(VSYNC_TEST):		$(VSYNC_TEST_OBJS)
			$(CC) $(LDFLAGS) -lm $(VSYNC_TEST_OBJS) -o $@
			@echo ""
			@echo "x----------------x"
			@echo "| vsync_test     |"
			@echo "x----------------x"
			@echo ""

$(SPORG_GFX):		$(SPORG_GFX_OBJS)
			$(CC) $(LDFLAGS) -shared $(SPORG_GFX_OBJS) -o $@
			@echo ""
			@echo "x----------------x"
			@echo "| sporg_gfx      |"
			@echo "x----------------x"
			@echo ""

$(SPORG_INPUT):	$(SPORG_INPUT_OBJS)
			$(CC) $(LDFLAGS) -shared -lX11 $(SPORG_INPUT_OBJS) -o $@
			@echo ""
			@echo "x----------------x"
			@echo "| sporg_input    |"
			@echo "x----------------x"
			@echo ""

$(LIB_CLIENT):	./platform/linux/messages.c.o ./platform/linux/client.c.o
			@mkdir -p ./lib
			$(AR) rcs ./lib/$(LIB_CLIENT) ./platform/linux/messages.c.o ./platform/linux/client.c.o
			@echo ""
			@echo "x----------------x"
			@echo "| libspr16_cl.a  |"
			@echo "x----------------x"
			@echo ""

install:
	@umask 022
	@install -dvm 0755  "$(DESTDIR)/$(CFGDIR)"
	@install -dvm 0755  "$(DESTDIR)/$(BINDIR)"
	@install -dvm 0755  "$(DESTDIR)/$(INCDIR)"
	@install -dvm 0755  "$(DESTDIR)/$(LIBDIR)"
	@install -dvm 0755  "$(DESTDIR)/lib/xorg/modules/drivers"
	@install -dvm 0755  "$(DESTDIR)/lib/xorg/modules/input"
	@install -Dvm 0755  "./spr16.h" "$(DESTDIR)/$(INCDIR)"
	@install -Dvm 0755  "./lib/$(LIB_CLIENT)" "$(DESTDIR)/$(LIBDIR)/"
	@install -Dvm 0755  "$(GTSCREEN)"    "$(DESTDIR)/$(BINDIR)/$(GTSCREEN)"
	@install -Dvm 0755  "$(TOUCHPAINT)"  "$(DESTDIR)/$(BINDIR)/$(TOUCHPAINT)"
	@install -Dvm 0755  "$(VSYNC_TEST)"  "$(DESTDIR)/$(BINDIR)/$(VSYNC_TEST)"
	@install -Dvm 0755  "$(LANDIT)"      "$(DESTDIR)/$(BINDIR)/$(LANDIT)"
	@install -Dvm 0755  airlock/sporg/xorg.conf "$(DESTDIR)/$(CFGDIR)"
	@install -Dvm 0755  "$(SPORG_GFX)"   \
		"$(DESTDIR)/$(XORG_MOD_DIR)/drivers/$(SPORG_GFX)"
	@install -Dvm 0755  "$(SPORG_INPUT)" \
		"$(DESTDIR)/$(XORG_MOD_DIR)/input/$(SPORG_INPUT)"

clean:
	@$(foreach obj, $(GTSCREEN_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(LANDIT_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(TOUCHPAINT_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(VSYNC_TEST_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPORG_GFX_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPORG_INPUT_OBJS), rm -fv $(obj);)

	@-rm -fv ./$(GTSCREEN)
	@-rm -fv ./lib/$(LIB_CLIENT)
	@-rm -fv ./$(LANDIT)
	@-rm -fv ./$(TOUCHPAINT)
	@-rm -fv ./$(VSYNC_TEST)
	@-rm -fv ./$(SPORG_GFX)
	@-rm -fv ./$(SPORG_INPUT)
	@echo "cleaned."

