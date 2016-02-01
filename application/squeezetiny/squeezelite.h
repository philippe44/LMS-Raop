/*
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2014, triode1@btinternet.com
 *  
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// make may define: SELFPIPE, RESAMPLE, RESAMPLE_MP, VISEXPORT, DSD, LINKALL to influence build

// build detection
#include "squeezedefs.h"

#if LINUX && !defined(SELFPIPE)
#define EVENTFD   1
#define SELFPIPE  0
#define WINEVENT  0
#endif
#if (LINUX && !EVENTFD) || OSX || FREEBSD
#define EVENTFD   0
#define SELFPIPE  1
#define WINEVENT  0
#endif
#if WIN
#define EVENTFD   0
#define SELFPIPE  0
#define WINEVENT  1
#endif

#if defined(RESAMPLE) || defined(RESAMPLE_MP)
#undef  RESAMPLE
#define RESAMPLE  1 // resampling
#define PROCESS   1 // any sample processing (only resampling at present)
#else
#define RESAMPLE  0
#define PROCESS   0
#endif
#if defined(RESAMPLE_MP)
#undef RESAMPLE_MP
#define RESAMPLE_MP 1
#else
#define RESAMPLE_MP 0
#endif

#if defined(FFMPEG)
#undef FFMPEG
#define FFMPEG    1
#else
#define FFMPEG    0
#endif


#if defined(LINKALL)
#undef LINKALL
#define LINKALL   1 // link all libraries at build time - requires all to be available at run time
#else
#define LINKALL   0
#endif


#if !LINKALL

// dynamically loaded libraries at run time

#if LINUX
#define LIBFLAC "libFLAC.so.8"
#define LIBMAD  "libmad.so.0"
#define LIBMPG "libmpg123.so.0"
#define LIBVORBIS "libvorbisfile.so.3"
#define LIBTREMOR "libvorbisidec.so.1"
#define LIBFAAD "libfaad.so.2"
#define LIBAVUTIL   "libavutil.so.%d"
#define LIBAVCODEC  "libavcodec.so.%d"
#define LIBAVFORMAT "libavformat.so.%d"
#define LIBSOXR "libsoxr.so.0"
#endif

#if OSX
#define LIBFLAC "libFLAC.8.dylib"
#define LIBMAD  "libmad.0.dylib"
#define LIBMPG "libmpg123.0.dylib"
#define LIBVORBIS "libvorbisfile.3.dylib"
#define LIBTREMOR "libvorbisidec.1.dylib"
#define LIBFAAD "libfaad.2.dylib"
#define LIBAVUTIL   "libavutil.%d.dylib"
#define LIBAVCODEC  "libavcodec.%d.dylib"
#define LIBAVFORMAT "libavformat.%d.dylib"
#define LIBSOXR "libsoxr.0.dylib"
#endif

#if WIN
#define LIBFLAC "libFLAC.dll"
#define LIBMAD  "libmad-0.dll"
#define LIBMPG "libmpg123-0.dll"
#define LIBVORBIS "libvorbisfile.dll"
#define LIBTREMOR "libvorbisidec.dll"
#define LIBFAAD "libfaad2.dll"
#define LIBAVUTIL   "avutil-%d.dll"
#define LIBAVCODEC  "avcodec-%d.dll"
#define LIBAVFORMAT "avformat-%d.dll"
#define LIBSOXR "libsoxr.dll"
#endif

#if FREEBSD
#define LIBFLAC "libFLAC.so.11"
#define LIBMAD  "libmad.so.2"
#define LIBMPG "libmpg123.so.0"
#define LIBVORBIS "libvorbisfile.so.6"
#define LIBTREMOR "libvorbisidec.so.1"
#define LIBFAAD "libfaad.so.2"
#define LIBAVUTIL   "libavutil.so.%d"
#define LIBAVCODEC  "libavcodec.so.%d"
#define LIBAVFORMAT "libavformat.so.%d"
#endif

#endif // !LINKALL

// config options
#define OUTPUTBUF_SIZE_CROSSFADE (OUTPUTBUF_SIZE * 12 / 10)

#define MAX_HEADER 4096 // do not reduce as icy-meta max is 4080

#define SL_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "squeezeitf.h"
#include "util_common.h"

#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

typedef u32_t frames_t;
typedef int sockfd;

#if EVENTFD
#include <sys/eventfd.h>
#define event_event int
#define event_handle struct pollfd
#define wake_create(e) e = eventfd(0, 0)
#define wake_signal(e) eventfd_write(e, 1)
#define wake_clear(e) eventfd_t val; eventfd_read(e, &val)
#define wake_close(e) close(e)
#endif

#if SELFPIPE
#define event_handle struct pollfd
#define event_event struct wake
#define wake_create(e) pipe(e.fds); set_nonblock(e.fds[0]); set_nonblock(e.fds[1])
#define wake_signal(e) write(e.fds[1], ".", 1)
#define wake_clear(e) char c[10]; read(e, &c, 10)
#define wake_close(e) close(e.fds[0]); close(e.fds[1])
struct wake { 
	int fds[2];
};
#endif

#if WINEVENT
#define event_event HANDLE
#define event_handle HANDLE
#define wake_create(e) e = CreateEvent(NULL, FALSE, FALSE, NULL)
#define wake_signal(e) SetEvent(e)
#define wake_close(e) CloseHandle(e)
#endif

// printf/scanf formats for u64_t
#if (LINUX && __WORDSIZE == 64) || (FREEBSD && __LP64__)
#define FMT_u64 "%lu"
#define FMT_x64 "%lx"
#elif __GLIBC_HAVE_LONG_LONG || defined __GNUC__ || WIN
#define FMT_u64 "%llu"
#define FMT_x64 "%llx"
#else
#error can not support u64_t
#endif

//#define MAX_SILENCE_FRAMES 2048
#define MAX_SILENCE_FRAMES 352
#define TIMEGAPS	8
#define FIXED_ONE 0x10000

#define BYTES_PER_FRAME 4

// utils.c (non logging)
typedef enum { EVENT_TIMEOUT = 0, EVENT_READ, EVENT_WAKE } event_type;
struct thread_ctx_s;

char *next_param(char *src, char c);
u32_t gettime_ms(void);
void get_mac(u8_t *mac);
void set_nonblock(sockfd s);
int connect_timeout(sockfd sock, const struct sockaddr *addr, socklen_t addrlen, int timeout);
void server_addr(char *server, in_addr_t *ip_ptr, unsigned *port_ptr);
void set_readwake_handles(event_handle handles[], sockfd s, event_event e);
event_type wait_readwake(event_handle handles[], int timeout);
void packN(u32_t *dest, u32_t val);
void packn(u16_t *dest, u16_t val);
u32_t unpackN(u32_t *src);
u16_t unpackn(u16_t *src);
#if OSX
void set_nosigpipe(sockfd s);
#else
#define set_nosigpipe(s)
#endif
#if WIN
void winsock_init(void);
void winsock_close(void);
void *dlopen(const char *filename, int flag);
void *dlsym(void *handle, const char *symbol);
char *dlerror(void);
int poll(struct pollfd *fds, unsigned long numfds, int timeout);
#endif
#if LINUX || FREEBSD
void touch_memory(u8_t *buf, size_t size);
#endif

// buffer.c
struct buffer {
	u8_t *buf;
	u8_t *readp;
	u8_t *writep;
	u8_t *wrap;
	size_t size;
	size_t base_size;
	mutex_type mutex;
};

// _* called with mutex locked
unsigned _buf_used(struct buffer *buf);
unsigned _buf_space(struct buffer *buf);
unsigned _buf_cont_read(struct buffer *buf);
unsigned _buf_cont_write(struct buffer *buf);
void _buf_inc_readp(struct buffer *buf, unsigned by);
void _buf_inc_writep(struct buffer *buf, unsigned by);
unsigned _buf_read(void *dst, struct buffer *src, unsigned btes);
void	*_buf_readp(struct buffer *buf);
int	 _buf_seek(struct buffer *src, unsigned from, unsigned by);
void _buf_move(struct buffer *buf, unsigned by);
unsigned _buf_size(struct buffer *src);
void buf_flush(struct buffer *buf);
void buf_adjust(struct buffer *buf, size_t mod);
void _buf_resize(struct buffer *buf, size_t size);
void buf_init(struct buffer *buf, size_t size);
void buf_destroy(struct buffer *buf);

// slimproto.c
void slimproto_close(struct thread_ctx_s *ctx);
void slimproto_reset(struct thread_ctx_s *ctx);
void slimproto_thread_init(char *server, u8_t mac[], const char *name, const char *namefile, struct thread_ctx_s *ctx);
void wake_controller(struct thread_ctx_s *ctx);
void send_packet(u8_t *packet, size_t len, sockfd sock);
void wake_controller(struct thread_ctx_s *ctx);

// stream.c
typedef enum { STOPPED = 0, DISCONNECT, STREAMING_WAIT,
			   STREAMING_BUFFERING, STREAMING_FILE, STREAMING_HTTP, SEND_HEADERS, RECV_HEADERS } stream_state;
typedef enum { DISCONNECT_OK = 0, LOCAL_DISCONNECT = 1, REMOTE_DISCONNECT = 2, UNREACHABLE = 3, TIMEOUT = 4 } disconnect_code;

struct streamstate {
	stream_state state;
	disconnect_code disconnect;
	char *header;
	size_t header_len;
	bool sent_headers;
	bool cont_wait;
	u64_t bytes;
	u32_t last_read;
	unsigned threshold;
	u32_t meta_interval;
	u32_t meta_next;
	u32_t meta_left;
	bool  meta_send;
};

void stream_thread_init(unsigned buf_size, struct thread_ctx_s *ctx);
void stream_close(struct thread_ctx_s *ctx);
void stream_file(const char *header, size_t header_len, unsigned threshold, struct thread_ctx_s *ctx);
void stream_sock(u32_t ip, u16_t port, const char *header, size_t header_len, unsigned threshold, bool cont_wait, struct thread_ctx_s *ctx);
bool stream_disconnect(struct thread_ctx_s *ctx);

// decode.c
typedef enum { DECODE_STOPPED = 0, DECODE_READY, DECODE_RUNNING, DECODE_COMPLETE, DECODE_ERROR } decode_state;

struct decodestate {
	decode_state state;
	bool new_stream;
	mutex_type mutex;
	void *handle;
#if PROCESS
	void *process_handle;
	bool direct;
	bool process;
#endif
};

#if PROCESS
struct processstate {
	u8_t *inbuf, *outbuf;
	unsigned max_in_frames, max_out_frames;
	unsigned in_frames, out_frames;
	unsigned in_sample_rate, out_sample_rate;
	unsigned long total_in, total_out;
};
#endif

struct codec {
	char id;
	char *types;
	unsigned min_read_bytes;
	unsigned min_space;
	void (*open)(u8_t sample_size, u32_t sample_rate, u8_t channels, u8_t endianness, struct thread_ctx_s *ctx);
	void (*close)(struct thread_ctx_s *ctx);
	decode_state (*decode)(struct thread_ctx_s *ctx);
};

void decode_init(void);
void decode_thread_init(struct thread_ctx_s *ctx);

void decode_close(struct thread_ctx_s *ctx);
void decode_flush(struct thread_ctx_s *ctx);
unsigned decode_newstream(unsigned sample_rate, unsigned supported_rates[], struct thread_ctx_s *ctx);
void codec_open(u8_t format, u8_t sample_size, u32_t sample_rate, u8_t channels, u8_t endianness, struct thread_ctx_s *ctx);

#if PROCESS
// process.c
void process_samples(struct thread_ctx_s *ctx);
void process_drain(struct thread_ctx_s *ctx);
void process_flush(struct thread_ctx_s *ctx);
unsigned process_newstream(bool *direct, unsigned raw_sample_rate, unsigned supported_rates[], struct thread_ctx_s *ctx);
void process_init(char *opt, struct thread_ctx_s *ctx);
void process_end(struct thread_ctx_s *ctx);
#endif

#if RESAMPLE
// resample.c
void resample_samples(struct thread_ctx_s *ctx);
bool resample_drain(struct thread_ctx_s *ctx);
bool resample_newstream(unsigned raw_sample_rate, unsigned supported_rates[], struct thread_ctx_s *ctx);
void resample_flush(struct thread_ctx_s *ctx);
bool resample_init(char *opt, struct thread_ctx_s *ctx);
void resample_end(struct thread_ctx_s *ctx);
#endif

// output.c output_pack.c
typedef enum { OUTPUT_OFF = -1, OUTPUT_STOPPED = 0, OUTPUT_BUFFER, OUTPUT_RUNNING,
			   OUTPUT_PAUSE_FRAMES, OUTPUT_SKIP_FRAMES, OUTPUT_START_AT } output_state;

typedef enum { DETECT_IDLE, DETECT_ACQUIRE, DETECT_STARTED } detect_state;

typedef enum { FADE_INACTIVE = 0, FADE_DUE, FADE_ACTIVE } fade_state;
typedef enum { FADE_UP = 1, FADE_DOWN, FADE_CROSS } fade_dir;
typedef enum { FADE_NONE = 0, FADE_CROSSFADE, FADE_IN, FADE_OUT, FADE_INOUT } fade_mode;


struct outputstate {
	output_state state;
	void *device;
	bool  track_started;
	int (* write_cb)(struct thread_ctx_s *ctx, frames_t out_frames, bool silence, s32_t gainL, s32_t gainR, s32_t cross_gain_in, s32_t cross_gain_out, s16_t **cross_ptr);
	unsigned start_frames;
	unsigned frames_played;
	unsigned frames_played_dmp;// frames played at the point delay is measured
	detect_state start_detect;
	unsigned current_sample_rate;
	unsigned default_sample_rate;
	unsigned supported_rates[2];
	bool error_opening;
	u32_t updated;
	u32_t track_start_time;
	u32_t current_replay_gain;
	// was union
	struct {
		u32_t pause_frames;
		u32_t skip_frames;
		u32_t start_at;
	};
	u8_t  *track_start;        // set in decode thread
	u32_t gainL;               // set by slimproto
	u32_t gainR;               // set by slimproto
	u32_t next_replay_gain;    // set by slimproto
	unsigned threshold;        // set by slimproto
	fade_state fade;
	u8_t *fade_start;
	u8_t *fade_end;
	fade_dir fade_dir;
	fade_mode fade_mode;       // set by slimproto
	unsigned fade_secs;        // set by slimproto
	bool delay_active;
	int buf_frames;
	u8_t *buf;
	u32_t latency;
	u32_t *timerefs;
	u16_t  nb_timerefs;
};

void output_init(const char *device, unsigned output_buf_size, unsigned rates[], struct thread_ctx_s *ctx);
void output_close(struct thread_ctx_s *ctx);
void output_flush(struct thread_ctx_s *ctx);
// _* called with mutex locked
frames_t _output_frames(frames_t avail, struct thread_ctx_s *ctx);
void _checkfade(bool, struct thread_ctx_s *ctx);
void wake_output(struct thread_ctx_s *ctx);

// output_raop.c
void output_init_common(void *device, unsigned output_buf_size, u32_t sample_rate, struct thread_ctx_s *ctx);
void output_raop_thread_init(struct raopcl_s *raopcl, unsigned output_buf_size, u32_t sample_rate, u8_t sample_size, struct thread_ctx_s *ctx);
void output_close_common(struct thread_ctx_s *ctx);

// output_pack.c
void _scale_frames(s16_t *outputptr, s16_t *inputptr, frames_t cnt, s32_t gainL, s32_t gainR);
void _apply_cross(struct buffer *outputbuf, frames_t out_frames, s32_t cross_gain_in, s32_t cross_gain_out, s16_t **cross_ptr);
inline s16_t gain(s32_t gain, s16_t sample);
s32_t gain32(s32_t gain, s32_t value);
s32_t to_gain(float f);

// dop.c
#if DSD
bool is_flac_dop(u32_t *lptr, u32_t *rptr, frames_t frames);
void update_dop_marker(u32_t *ptr, frames_t frames);
void dop_silence_frames(u32_t *ptr, frames_t frames);
void dop_init(bool enable, unsigned delay);
#endif


/***************** main thread context**************/
typedef struct {
	u32_t updated;
	u32_t stream_start;			// vf : now() when stream started
	u32_t stream_full;			// v : unread bytes in stream buf
	u32_t stream_size;			// f : stream_buf_size init param
	u64_t stream_bytes;         // v : bytes received for current stream
	u32_t output_full;			// v : unread bytes in output buf
	u32_t output_size;			// f : output_buf_size init param
	u32_t frames_played;        // number of samples (bytes / sample size) played
	u32_t current_sample_rate;
	u32_t last;
	stream_state stream_state;
} status_t;

typedef enum {TRACK_STOPPED = 0, TRACK_STARTED, TRACK_PAUSED} track_status_t;

#define PLAYER_NAME_LEN 64
#define SERVER_NAME_LEN	250
#define SERVER_VERSION_LEN	32
#define MAX_PLAYER		32


struct thread_ctx_s {
	int 	self;
	int 	autostart;
	bool	running;
	bool	in_use;
	bool	on;
	char   last_command;
	sq_dev_param_t	config;
	mutex_type mutex;
	bool 	sentSTMu, sentSTMo, sentSTMl, sentSTMd;
	u32_t 	new_server;
	char 	*new_server_cap;
	char	fixed_cap[128], var_cap[128];
	char 	player_name[PLAYER_NAME_LEN + 1];
	status_t			status;
	struct streamstate	stream;
	struct outputstate 	output;
	struct decodestate 	decode;
#if PROCESS
	struct processstate	process;
#endif
	struct codec		*codec;
	struct buffer		__s_buf;
	struct buffer		__o_buf;
	struct buffer		*streambuf;
	struct buffer		*outputbuf;
	unsigned outputbuf_size, streambuf_size;
	in_addr_t 	slimproto_ip;
	unsigned 	slimproto_port;
	char		server[SERVER_NAME_LEN + 1];
	char		server_version[SERVER_VERSION_LEN + 1];
	char		server_port[5+1];
	char		server_ip[4*(3+1)+1];
	sockfd 		sock, fd, cli_sock;
	u8_t 		mac[6];
	char		cli_id[18];		// (6*2)+(5*':')+NULL
	mutex_type	cli_mutex;
	int bytes_per_frame;		// for output
	bool	output_running;		// for output.c
	bool	stream_running;		// for stream.c
	bool	decode_running;		// for decode.c
	thread_type output_thread;	// output.c child thread
	thread_type stream_thread;	// stream.c child thread
	thread_type decode_thread;	// decode.c child thread
	thread_type	thread;			// main instance thread
	struct sockaddr_in serv_addr;
	#define MAXBUF 4096
	event_event	wake_e;
	struct 	{				// scratch memory for slimprot_run (was static)
		 u8_t 	buffer[MAXBUF];
		 u32_t	last;
		 char	header[MAX_HEADER];
	} slim_run;
	sq_callback_t	callback;
	void			*MR;
	u8_t *silencebuf;
};

extern struct thread_ctx_s thread_ctx[MAX_PLAYER];

// codecs
#define MAX_CODECS 4

extern struct codec *codecs[MAX_CODECS];

struct codec *register_flac(void);
struct codec *register_pcm(void);
struct codec *register_mad(void);
struct codec *register_mpg(void);
struct codec *register_vorbis(void);
struct codec *register_faad(void);
struct codec *register_dsd(void);
struct codec *register_ff(const char *codec);
#if RESAMPLE
bool register_soxr(void);
#endif



