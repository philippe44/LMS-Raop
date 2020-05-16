BASE		= ..

ifeq ($(OS),Darwin)
LINKSTATIC	=
LDFLAGS 	+= -Wl,-dead_strip
else
ifeq ($(OS),SunOS)
LINKSTATIC 	=
else
LINKSTATIC 	= -static
endif
LDFLAGS 	+= -Wl,--gc-sections
endif

TOOLS		= ./tools
SRC			= ./squeeze2raop
SQUEEZETINY	= ./squeezetiny

RAOP		= $(BASE)/raop_play
VALGRIND	= $(BASE)/valgrind
UPNP 		= $(BASE)/libupnp/1.6.19
MDNSSD		= $(BASE)/mdnssd
FETCHER		= $(BASE)/http_fetcher
TINYMDNS	= $(BASE)/tinysvcmdns
ALAC		= $(BASE)/alac
CURVE25519	= $(BASE)/curve25519/source

DEFINES 	+= -DHAVE_STDINT_H -DRESAMPLE -DNDEBUG -DUSE_SSL -D_GNU_SOURCE

vpath %.c $(TOOLS):$(SRC):$(RAOP)/src:$(SQUEEZETINY):$(MDNSSD):$(FETCHER)/src:$(TINYMDNS):$(ALAC):$(CURVE25519)
vpath %.cpp $(TOOLS):$(SRC):$(RAOP)/src:$(SQUEEZETINY):$(MDNSSD):$(FETCHER)/src:$(TINYMDNS):$(ALAC):$(CURVE25519)

LIBRARY 		= $(OBJ)/libixml.a $(OBJ)/libsoxr.a
LIBRARY_STATIC 	= $(LIBRARY) $(OBJ)/libfaad.a $(OBJ)/libsoxr.a $(OBJ)/libFLAC.a $(OBJ)/libmad.a $(OBJ)/libvorbisfile.a $(OBJ)/libopusfile.a $(OBJ)/libopus.a $(OBJ)/libvorbis.a $(OBJ)/libogg.a -lssl -lcrypto

INCLUDE = -I. \
		  -I$(SQUEEZETINY) \
		  -I$(RAOP)/src \
		  -I$(TOOLS) \
		  -I$(SRC) -I$(SRC)/inc \
		  -I$(VALGRIND)/memcheck -I$(VALGRIND)/include \
		  -I$(UPNP)/ixml/inc -I$(UPNP)/upnp/inc \
		  -I$(MDNSSD)\
		  -I$(BASE)/mpg123/src/libmpg123\
		  -I$(BASE)/flac/include\
		  -I$(BASE)/faad2/include\
		  -I$(BASE)/libmad\
		  -I$(BASE)/libvorbis/include -I$(BASE)/libogg/include\
		  -I$(BASE)/libopus/include -I$(BASE)/libopusfile/include\
		  -I$(BASE)/soxr/src\
		  -I$(FETCHER)/include\
		  -I$(TINYMDNS)\
		  -I$(ALAC)\
		  -I$(CURVE25519) -I$(CURVE25519)/include

DEPS	= $(SQUEEZETINY)/squeezedefs.h
		  
SOURCES = slimproto.c buffer.c stream.c utils.c pcm.c process.c resample.c alac.c \
		output.c decode.c output_pack.c output_raop.c  main.c util_common.c \
		raop_client.c rtsp_client.c aes.c aexcl_lib.c base64.c alac_wrapper.cpp aes_ctr.c \
      	squeeze2raop.c util.c conf_util.c log_util.c \
		mdnssd-min.c \
		http_fetcher.c http_error_codes.c \
		mdns.c mdnsd.c\
		ag_dec.c ag_enc.c ALACBitUtilities.c ALACEncoder.cpp dp_enc.c EndianPortable.c matrix_enc.c\
		ALACDecoder.cpp dp_dec.c matrix_dec.c \
		curve25519_dh.c curve25519_mehdi.c curve25519_order.c curve25519_utils.c custom_blind.c\
		ed25519_sign.c ed25519_verify.c
		
SOURCES_LIBS = mpg.c flac.c mad.c faad.c vorbis.c opus.c sslsym.c
		
OBJECTS 		= $(patsubst %.c,$(OBJ)/%.o,$(filter %.c,$(SOURCES))) $(patsubst %.cpp,$(OBJ)/%.o,$(filter %.cpp,$(SOURCES))) $(patsubst %.c,$(OBJ)/%.o,$(SOURCES_LIBS)) 
OBJECTS_STATIC 	= $(patsubst %.c,$(OBJ)/%.o,$(filter %.c,$(SOURCES))) $(patsubst %.cpp,$(OBJ)/%.o,$(filter %.cpp,$(SOURCES))) $(patsubst %.c,$(OBJ)/%-static.o,$(SOURCES_LIBS)) 

all: $(EXECUTABLE) $(EXECUTABLE_STATIC)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBRARY) $(LDFLAGS) -o $@
	
$(EXECUTABLE_STATIC): $(OBJECTS_STATIC)
	$(CC) $(OBJECTS_STATIC) $(LIBRARY_STATIC) $(LDFLAGS) $(LINKSTATIC) -o $@	

$(OBJECTS): $(DEPS) | bin $(OBJ)

$(OBJ):
	@mkdir -p $@
	
bin:	
	@mkdir -p bin

$(OBJ)/%.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@
	
$(OBJ)/%.o : %.cpp
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@
	
$(OBJ)/%-static.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DLINKALL $(INCLUDE) $< -c -o $(OBJ)/$*-static.o	
	
clean:
	rm -f $(OBJECTS) $(OBJECTS_STATIC) $(EXECUTABLE) $(EXECUTABLE_STATIC)

