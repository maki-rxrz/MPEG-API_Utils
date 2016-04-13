BIN_DIR  = ../bin
API_SRCS = mpeg_utils.c mpeges_parser.c mpegts_parser.c mpeg_stream.c mpeg_video.c thread_utils.c file_reader.c file_utils.c

TARGET1 = $(BIN_DIR)/cutcaption.exe
SRCS1   = cutcaption.c d2v_parser.c text_utils.c avs_utils.c
OBJS1   = $(SRCS1:%.c=%.o)

TARGET2 = $(BIN_DIR)/ts_parser.exe
SRCS2   = ts_parser.c thread_utils.c file_writer.c file_utils.c
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

DEP_CC  =  $(CROSS)gcc
CC      = @$(CROSS)gcc
LD      = @$(CROSS)gcc
STRIP   = @$(CROSS)strip
AR      = @$(CROSS)ar
RANLIB  = @$(CROSS)ranlib
DLLTOOL = @$(CROSS)dlltool
LIBTOOL = @libtool
CP      = @cp -p
RM      = rm -rf

CFLAGS  = -Wall -std=c99 -fvisibility=hidden $(XCFLAGS)
LDFLAGS = $(XLDFLAGS)
LIBS    =

ifeq ($(THREAD_LIBS), win32)
CFLAGS += -DMAPI_WTHREAD_ENABLED
else
CFLAGS += -DMAPI_PTHREAD_ENABLED
LIBS   += -lpthread
endif

ifeq ($(findstring clang, $(CC)), clang)
override DEP_CC :=  $(CC)
override CC     := @$(CC)
override LD     := @$(LD)
CFLAGS += -Wno-missing-field-initializers
endif

.PHONY: all lib init clean

all: init $(TARGET1) $(TARGET2)

lib: init $(STATIC_LIB) $(DYNAMIC_LIB)

init:
	@mkdir -p "$(BIN_DIR)" "$(SLIB_DIR)" "$(DLIB_DIR)"

$(STATIC_LIB): $(SLIB_OBJS)
	@echo "  AR        $(STATIC_LIB)"
	$(AR) rc $(STATIC_LIB) $(SLIB_OBJS)
	@echo "  RANLIB    $(STATIC_LIB)"
	$(RANLIB) $(STATIC_LIB)
	$(CP) $(STATIC_LIB) $(BIN_DIR)

$(DYNAMIC_LIB): $(DLIB_OBJS)
	@echo "  LD        $(DYNAMIC_LIB)"
	$(LD) $(LDFLAGS) $(DLIB_OBJS) $(LIBS) -shared -Wl,--out-implib,$(DYNAMIC_LIB).a -o $(DYNAMIC_LIB)
	$(CP) $(DYNAMIC_LIB)* $(BIN_DIR)
	$(STRIP) $(BIN_DIR)/$(DYNAMIC_LIB)

$(SLIB_DIR)/%.o: %.c .depend
	@echo "  CC        $<"
	$(CC) -c $(CFLAGS) -DMAPI_INTERNAL_CODE_ENABLED $< -o $@

$(DLIB_DIR)/%.o: %.c .depend
	@echo "  CC        $<"
	$(CC) -c $(CFLAGS) -DMAPI_INTERNAL_CODE_ENABLED -DMAPI_DLL_EXPORT $< -o $@

$(TARGET1): $(OBJS1) $(STATIC_LIB)
	@echo "  LD        $(TARGET1)"
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

$(TARGET2): $(OBJS2) $(STATIC_LIB)
	@echo "  LD        $(TARGET2)"
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

%.o: %.c .depend
	@echo "  CC        $<"
	$(CC) -c $(CFLAGS) -DMAPI_UTILS_CODE_ENABLED $< -o $@

clean:
	$(RM) "$(SLIB_DIR)" "$(DLIB_DIR)" *.o config.* .depend *.a *.lib *.dll* *.exp

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(DEP_CC) $(SRC) $(CFLAGS) -MT $(SRC:%.c=%.o) -MM >> .depend;)
