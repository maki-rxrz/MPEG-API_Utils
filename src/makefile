BIN_DIR  = ../bin
API_SRCS = mpeg_utils.c mpeges_parser.c mpegts_parser.c mpeg_stream.c mpeg_video.c

TARGET1 = $(BIN_DIR)/cutcaption.exe
SRCS1   = cutcaption.c d2v_parser.c text_utils.c avs_utils.c
OBJS1   = $(SRCS1:%.c=%.o)

TARGET2 = $(BIN_DIR)/ts_parser.exe
SRCS2   = ts_parser.c thread_utils.c
OBJS2   = $(SRCS2:%.c=%.o)

LIB_NAME    = libmapi
STATIC_LIB  = $(LIB_NAME).a
DYNAMIC_LIB = $(LIB_NAME).dll
LIB_SRCS    = $(LIB_NAME).c $(API_SRCS)
SLIB_DIR    = .slib
DLIB_DIR    = .dlib
SLIB_OBJS   = $(LIB_SRCS:%.c=$(SLIB_DIR)/%.o)
DLIB_OBJS   = $(LIB_SRCS:%.c=$(DLIB_DIR)/%.o)

SRCS = $(SRCS1) $(SRCS2) $(LIB_SRCS)

CC      = $(CROSS)gcc
LD      = $(CROSS)gcc
STRIP   = $(CROSS)strip
AR      = $(CROSS)ar
RANLIB  = $(CROSS)ranlib
DLLTOOL = $(CROSS)dlltool
LIBTOOL = libtool
RM      = rm -rf

CFLAGS  = -Wall -std=gnu99 $(ECFLAGS)
LDFLAGS = $(ELDFLAGS)
LIBS    = -lpthread

.PHONY: all lib init clean

all: init $(TARGET1) $(TARGET2)

lib: init $(STATIC_LIB) $(DYNAMIC_LIB)

init:
	@mkdir -p "$(BIN_DIR)" "$(SLIB_DIR)" "$(DLIB_DIR)"

$(STATIC_LIB): $(SLIB_OBJS)
	$(AR) rc $(STATIC_LIB) $(SLIB_OBJS)
	$(RANLIB) $(STATIC_LIB)
	cp -p $(STATIC_LIB) $(BIN_DIR)

$(DYNAMIC_LIB): $(DLIB_OBJS)
	$(LD) $(LDFLAGS) $(DLIB_OBJS) -shared -Wl,--out-implib,$(DYNAMIC_LIB).a -o $(DYNAMIC_LIB)
	cp -p $(DYNAMIC_LIB)* $(BIN_DIR)
	$(STRIP) $(BIN_DIR)/$(DYNAMIC_LIB)

$(SLIB_DIR)/%.o: %.c .depend
	$(CC) -c $(CFLAGS) -DMAPI_INTERNAL_CODE_ENABLED $< -o $@

$(DLIB_DIR)/%.o: %.c .depend
	$(CC) -c $(CFLAGS) -DMAPI_INTERNAL_CODE_ENABLED -DMAPI_DLL_EXPORT $< -o $@

$(TARGET1): $(OBJS1) $(STATIC_LIB)
	$(LD) $(LDFLAGS) -o $@ $^
	$(STRIP) $@

$(TARGET2): $(OBJS2) $(STATIC_LIB)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

%.o: %.c .depend
	$(CC) -c $(CFLAGS) -DMAPI_UTILS_CODE_ENABLED $< -o $@

clean:
	$(RM) "$(SLIB_DIR)" "$(DLIB_DIR)" *.o config.* .depend *.a *.lib *.dll* *.exp

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(CC) $(SRC) $(CFLAGS) -MT $(SRC:%.c=%.o) -MM >> .depend;)
