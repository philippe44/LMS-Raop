ifeq ($(CC),cc)
CC=$(lastword $(subst /, ,$(shell readlink -f `which cc`)))
endif

PLATFORM ?= $(firstword $(subst -, ,$(CC)))
HOST ?= $(word 2, $(subst -, ,$(CC)))

ifeq ($(OS),Darwin)
LINKSTATIC	=
LDFLAGS 	+= -Wl,-dead_strip
else
LINKSTATIC 	= -static
LDFLAGS 	+= -Wl,--gc-sections
endif

BUILDDIR	= build/$(PLATFORM)
EXECUTABLE 	= bin/squeeze2raop-$(PLATFORM)
EXECUTABLE_STATIC = bin/squeeze2raop-$(PLATFORM)-static

SRC		= squeeze2raop
SQUEEZELITE	= squeezelite
TOOLS		= crosstools/src
MDNS		= libmdns/targets
PUPNP 		= libpupnp/targets/$(HOST)/$(PLATFORM)
CODECS		= libcodecs/targets
OPENSSL		= libopenssl/targets/$(HOST)/$(PLATFORM)
RAOP		= libraop/targets
JANSSON		= libjansson/targets/$(HOST)/$(PLATFORM)
FETCHER		= http-fetcher
#VALGRIND	= $(BASE)/valgrind

DEFINES 	= -DCODECS -DUSE_SSL -D_GNU_SOURCE -DUPNP_STATIC_LIB -DLINKALL

CFLAGS  	+= -Wall -Wno-deprecated-declarations -Wno-format-truncation -Wno-stringop-truncation -fPIC -ggdb -O2 $(DEFINES) -fdata-sections -ffunction-sections 
LDFLAGS += -s -lpthread -ldl -lm -lrt -lstdc++ -L. 

vpath %.c $(TOOLS):$(SRC):$(SQUEEZELITE):$(FETCHER)/src
vpath %.cpp $(TOOLS):$(SRC):$(SQUEEZELITE):$(FETCHER)/src

INCLUDE = -I$(OPENSSL)/include \
		  -I$(TOOLS) \
		  -I$(SQUEEZELITE) \
		  -I$(RAOP)/include \
		  -I$(SRC)/inc \
		  -I$(PUPNP)/include/upnp -I$(PUPNP)/include/ixml -I$(PUPNP)/include/addons \
		  -I$(CODECS)/include/flac -I$(CODECS)/include/mad -I$(CODECS)/include/addons \
		  -I$(MDNS)/include/tinysvcmdns -I$(MDNS)/include/mdnssd \
		  -I$(FETCHER)/include
		  		  
DEPS	= $(SRC)/inc/squeezedefs.h
				  
SOURCES = slimproto.c buffer.c output.c output_pack.c output_raop.c main.c \
		  stream.c decode.c pcm.c resample.c process.c alac.c flac.c mad.c \
		  utils.c metadata.c \
		  cross_util.c cross_log.c cross_net.c cross_thread.c platform.c \
		  http_fetcher.c http_error_codes.c \
		  config_raop.c squeeze2raop.c

SOURCES_LIBS = cross_ssl.c
		
OBJECTS	= $(patsubst %.c,$(BUILDDIR)/%.o,$(filter %.c,$(SOURCES) $(SOURCES_LIBS)))
OBJECTS	+= $(patsubst %.c,$(BUILDDIR)/%.o,$(filter %.cpp,$(SOURCES) $(SOURCES_LIBS)))
OBJECTS_STATIC = $(patsubst %.c,$(BUILDDIR)/%.o,$(filter %.c,$(SOURCES))) $(patsubst %.c,$(BUILDDIR)/%-static.o,$(filter %.c,$(SOURCES_LIBS)))
OBJECTS_STATIC	+= $(patsubst %.cpp,$(BUILDDIR)/%.o,$(filter %.cpp,$(SOURCES))) $(patsubst %.cpp,$(BUILDDIR)/%-static.o,$(filter %.cpp,$(SOURCES_LIBS)))

LIBRARY	= $(PUPNP)/libpupnp.a \
		 $(CODECS)/$(HOST)/$(PLATFORM)/libcodecs.a \
		 $(MDNS)/$(HOST)/$(PLATFORM)/libmdns.a  \
		 $(RAOP)/$(HOST)/$(PLATFORM)/libraop.a
		 
LIBRARY_STATIC 	= $(LIBRARY) $(OPENSSL)/libopenssl.a

all: directory $(EXECUTABLE) $(EXECUTABLE_STATIC)

$(EXECUTABLE): $(OBJECTS) $(DEPS)
	$(CC) $(OBJECTS) $(LIBRARY) $(LDFLAGS) -o $@
	
$(EXECUTABLE_STATIC): $(OBJECTS_STATIC) $(DEPS)
	$(CC) $(OBJECTS_STATIC) $(LIBRARY_STATIC) $(LDFLAGS) -static -o $@	

directory:
	@mkdir -p $(BUILDDIR)
	@mkdir -p bin

$(BUILDDIR)/%.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@

$(BUILDDIR)/%-static.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSSL_LIB_STATIC $(INCLUDE) $< -c -o $(BUILDDIR)/$*-static.o	
	
clean:
	rm -f $(OBJECTS) $(EXECUTABLE) $(OBJECTS_STATIC) $(EXECUTABLE_STATIC) 
