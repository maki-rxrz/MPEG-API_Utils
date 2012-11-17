BIN_DIR  = ../bin
API_SRCS = mpeg_utils.c mpeges_parser.c mpegts_parser.c mpeg_stream.c mpeg_video.c
SRCS     = ts_parser.c thread_utils.c cutcaption.c d2v_parser.c avs_utils.c $(API_SRCS)

TARGET1 = $(BIN_DIR)/cutcaption.exe
SRCS1   = cutcaption.c d2v_parser.c avs_utils.c
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

CC      = $(CROSS)gcc
LD      = $(CROSS)gcc
STRIP   = $(CROSS)strip
AR      = $(CROSS)ar
RANLIB  = $(CROSS)ranlib
DLLTOOL = $(CROSS)dlltool
LIBTOOL = libtool

CFLAGS  = -Wall -std=gnu99 #-fvisibility=hidden
LDFLAGS =
LIBS    = -lpthread

.PHONY: all clean

all: $(TARGET1) $(TARGET2)

initialize:
	@if [ ! -d "$(SLIB_DIR)" ]; then  mkdir "$(SLIB_DIR)"; fi
	@if [ ! -d "$(DLIB_DIR)" ]; then  mkdir "$(DLIB_DIR)"; fi

lib: $(STATIC_LIB) $(DYNAMIC_LIB)

$(STATIC_LIB): initialize $(SLIB_OBJS)
	$(AR) rc $(STATIC_LIB) $(SLIB_OBJS)
	$(RANLIB) $(STATIC_LIB)
	cp -p $(STATIC_LIB) $(BIN_DIR)

$(DYNAMIC_LIB): initialize $(DLIB_OBJS)
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
	$(RM) $(SLIB_DIR)/*.o $(DLIB_DIR)/*.o *.o config.* .depend *.a *.lib *.dll* *.exp
	@if [ -d "$(SLIB_DIR)" ]; then  rmdir "$(SLIB_DIR)"; fi
	@if [ -d "$(DLIB_DIR)" ]; then  rmdir "$(DLIB_DIR)"; fi

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(CC) $(SRC) $(CFLAGS) -MT $(SRC:%.c=%.o) -MM >> .depend;)
