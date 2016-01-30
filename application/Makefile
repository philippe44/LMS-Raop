# Cross compile support - create a Makefile which defines these three variables and then includes this Makefile...
#CC = arm-linux-gnueabi-gcc
CFLAGS  ?= -Wall -fPIC -ggdb -O2 $(OPTS) $(INCLUDE) $(DEFINES)
#CFLAGS  ?= -pg -Wall -fPIC -ggdb $(OPTS) $(INCLUDE) $(DEFINES)
#LDFLAGS ?= -pg -lpthread -lssl -lcrypto -ldl -lm -lrt -L. 
LDFLAGS ?= -lpthread -lssl -lcrypto -ldl -lm -lrt -L. 
# for LD debug -s
EXECUTABLE ?= squeeze2raop-x86
EXECUTABLE_STATIC ?= squeeze2raop-x86-static

squeezetiny_dir = squeezetiny
squeezeraop_dir = squeeze2raop

build_dir	= ./bin

LIBRARY = libixml.a 

DEFINES = -DHAVE_STDINT_H
#-D_LARGE_FILE_SOURCE

INCLUDE = -I../../libupnp/1.6.19/ixml/inc\
          -I../../libupnp/1.6.19/upnp/inc\
		  -I../../mdnssd\
		  -I.\
		  -I$(squeezetiny_dir)\
		  -I$(squeezeraop_dir)\
          -I$(squeezeraop_dir)/inc

SOURCES = \
		$(squeezetiny_dir)/slimproto.c $(squeezetiny_dir)/buffer.c \
        $(squeezetiny_dir)/stream.c $(squeezetiny_dir)/utils.c \
        $(squeezetiny_dir)/pcm.c \
		$(squeezetiny_dir)/output.c $(squeezetiny_dir)/decode.c \
		$(squeezetiny_dir)/output_pack.c $(squeezetiny_dir)/output_raop.c \
        $(squeezetiny_dir)/main.c $(squeezetiny_dir)/util_common.c \
		$(squeezeraop_dir)/raop_util.c $(squeezeraop_dir)/raop_core.c \
		$(squeezeraop_dir)/raop_client.c $(squeezeraop_dir)/rtsp_client.c \
		$(squeezeraop_dir)/aes.c $(squeezeraop_dir)/aexcl_lib.c \
		$(squeezeraop_dir)/base64.c $(squeezeraop_dir)/alac_wrapper.c \
      	$(squeezeraop_dir)/util.c  \
	    $(squeezeraop_dir)/squeeze2raop.c\
		../../mdnssd/mdnssd-min.c	

DEPS             = $(squeezetiny_dir)/squeezelite.h $(squeezetiny_dir)/slimproto.h\
                   $(squeezetiny_dir)/squeezedefs.h $(squeezetiny_dir)/squeezeitf.h\
				   platform.h

UNAME            = $(shell uname -s)

# add optional sources
ifneq (,$(findstring $(OPT_DSD), $(CFLAGS)))
	SOURCES += $(SOURCES_DSD)
endif
ifneq (,$(findstring $(OPT_FF), $(CFLAGS)))
	SOURCES += $(SOURCES_FF)
endif
ifneq (,$(findstring $(OPT_RESAMPLE), $(CFLAGS)))
	SOURCES += $(SOURCES_RESAMPLE)
endif
ifneq (,$(findstring $(OPT_VIS), $(CFLAGS)))
	SOURCES += $(SOURCES_VIS)
endif

# add optional link options
ifneq (,$(findstring $(OPT_LINKALL), $(CFLAGS)))
	LDFLAGS += $(LINKALL)
ifneq (,$(findstring $(OPT_FF), $(CFLAGS)))
	LDFLAGS += $(LINKALL_FF)
endif
ifneq (,$(findstring $(OPT_RESAMPLE), $(CFLAGS)))
	LDFLAGS += $(LINKALL_RESAMPLE)
endif
else
# if not LINKALL and linux add LINK_LINUX
ifeq ($(UNAME), Linux)
	LDFLAGS += $(LINK_LINUX)
endif
endif

OBJECTS = $(SOURCES:.c=.o)

all: $(EXECUTABLE) $(EXECUTABLE_STATIC)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBRARY) $(LDFLAGS) -o $(build_dir)/$@

$(EXECUTABLE_STATIC): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBRARY) $(LDFLAGS) -static -o $(build_dir)/$@

$(OBJECTS): $(DEPS)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -c -o $@


clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

