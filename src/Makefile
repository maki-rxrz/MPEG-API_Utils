BIN_DIR  = ../bin
API_SRCS = common.c mpeg_utils.c mpeges_parser.c mpegts_parser.c mpeg_stream.c mpeg_video.c thread_utils.c file_reader.c

TARGET1 = $(BIN_DIR)/cutcaption.exe
SRCS1   = cutcaption.c common.c d2v_parser.c text_utils.c avs_utils.c
OBJS1   = $(SRCS1:%.c=%.o)

TARGET2 = $(BIN_DIR)/ts_parser.exe
SRCS2   = ts_parser.c common.c thread_utils.c file_writer.c
OBJS2   = $(SRCS2:%.c=%.o)

LIB_NAME    = libmapi
STATIC_LIB  = $(LIB_NAME).a
DYNAMIC_LIB = $(LIB_NAME).dll
LIB_SRCS    = $(LIB_NAME).c $(API_SRCS)
SLIB_DIR    = .slib
DLIB_DIR    = .dlib
SLIB_OBJS   = $(LIB_SRCS:%.c=$(SLIB_DIR)/%.o)
DLIB_OBJS   = $(LIB_SRCS:%.c=$(DLIB_DIR)/%.o)

SRCS = $(sort $(LIB_SRCS) $(SRCS1) $(SRCS2))

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

CFLAGS  = -Wall -Wextra -std=c99 -fvisibility=hidden -D_GNU_SOURCE=1 $(XCFLAGS)
LDFLAGS = $(XLDFLAGS)
LIBS    =

ifeq ($(ENABLE_SHARED), yes)
MAPILIB = $(DYNAMIC_LIB).a
LIBFALG = -DMAPI_DLL_IMPORT
else
MAPILIB = $(STATIC_LIB)
LIBFALG =
endif

ifeq ($(THREAD_LIBS), mcf)
CFLAGS += -DMAPI_MCFGTHREAD_ENABLED
LIBS   += -lmcfgthread
endif
ifeq ($(THREAD_LIBS), win32)
CFLAGS += -DMAPI_WIN32THREAD_ENABLED
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

$(DYNAMIC_LIB).a: $(DYNAMIC_LIB)

$(SLIB_DIR)/%.o: %.c .depend
	@echo "  CC        $<"
	$(CC) -c $(CFLAGS) -DMAPI_INTERNAL_CODE_ENABLED $< -o $@

$(DLIB_DIR)/%.o: %.c .depend
	@echo "  CC        $<"
	$(CC) -c $(CFLAGS) -DMAPI_INTERNAL_CODE_ENABLED -DMAPI_DLL_EXPORT $< -o $@

$(TARGET1): $(OBJS1) $(MAPILIB)
	@echo "  LD        $(TARGET1)"
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

$(TARGET2): $(OBJS2) $(MAPILIB)
	@echo "  LD        $(TARGET2)"
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(STRIP) $@

%.o: %.c .depend
	@echo "  CC        $<"
	$(CC) -c $(CFLAGS) -DMAPI_UTILS_CODE_ENABLED $(LIBFALG) $< -o $@

clean:
	$(RM) "$(SLIB_DIR)" "$(DLIB_DIR)" *.o config.* .depend *.a *.lib *.dll* *.exp

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(DEP_CC) $(SRC) $(CFLAGS) -MT $(SRC:%.c=%.o) -MM >> .depend;)
