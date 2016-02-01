/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *  (c) Philippe, philippe_44@outlook.com
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

#include "squeezelite.h"

#include <mpg123.h>

#define READ_SIZE  512
#define WRITE_SIZE 32 * 1024

//mpg123_handle *h;

#if !LINKALL
static struct {
	// mpg symbols to be dynamically loaded
	int (* mpg123_init)(void);
	int (* mpg123_feature)(const enum mpg123_feature_set);
	void (* mpg123_rates)(const long **, size_t *);
	int (*  mpg123_param)(mpg123_handle *, enum mpg123_parms type, long value, double fvalue);
	int (*  mpg123_getparam)(mpg123_handle *, enum mpg123_parms type, long *value, double *fvalue);
	int (* mpg123_format_none)(mpg123_handle *);
	int (* mpg123_format)(mpg123_handle *, long, int, int);
	mpg123_handle *(* mpg123_new)(const char*, int *);
	void (* mpg123_delete)(mpg123_handle *);
	int (* mpg123_open_feed)(mpg123_handle *);
	int (* mpg123_decode)(mpg123_handle *, const unsigned char *, size_t, unsigned char *, size_t, size_t *);
	int (* mpg123_getformat)(mpg123_handle *, long *, int *, int *);
	const char* (* mpg123_plain_strerror)(int);
} m;
#endif

extern log_level decode_loglevel;
static log_level *loglevel = &decode_loglevel;

#define LOCK_S   mutex_lock(ctx->streambuf->mutex)
#define UNLOCK_S mutex_unlock(ctx->streambuf->mutex)
#define LOCK_O   mutex_lock(ctx->outputbuf->mutex)
#define UNLOCK_O mutex_unlock(outputbuf->mutex)
#if PROCESS
#define LOCK_O_direct   if (ctx->decode.direct) mutex_lock(ctx->outputbuf->mutex)
#define UNLOCK_O_direct if (ctx->decode.direct) mutex_unlock(ctx->outputbuf->mutex)
#define LOCK_O_not_direct   if (!ctx->decode.direct) mutex_lock(ctx->outputbuf->mutex)
#define UNLOCK_O_not_direct if (!ctx->decode.direct) mutex_unlock(ctx->outputbuf->mutex)
#define IF_DIRECT(x)    if (ctx->decode.direct) { x }
#define IF_PROCESS(x)   if (!ctx->decode.direct) { x }
#else
#define LOCK_O_direct   mutex_lock(ctx->outputbuf->mutex)
#define UNLOCK_O_direct mutex_unlock(ctx->outputbuf->mutex)
#define LOCK_O_not_direct
#define UNLOCK_O_not_direct
#define IF_DIRECT(x)    { x }
#define IF_PROCESS(x)
#endif

#if LINKALL
#define MPG123(h, fn, ...) (mpg123_ ## fn)(__VA_ARGS__)
#else
#define MPG123(h, fn, ...) (h)->mpg123_##fn(__VA_ARGS__)
#endif

static decode_state mpg_decode(struct thread_ctx_s *ctx) {
	size_t bytes, space, size;
	int ret;
	u8_t *write_buf;

	LOCK_S;
	LOCK_O_direct;
	bytes = min(_buf_used(ctx->streambuf), _buf_cont_read(ctx->streambuf));

	IF_DIRECT(
		space = min(_buf_space(ctx->outputbuf), _buf_cont_write(ctx->outputbuf));
		write_buf = ctx->outputbuf->writep;
	);
	IF_PROCESS(
		space = ctx->process.max_in_frames;
		write_buf = ctx->process.inbuf;
	);

	bytes = min(bytes, READ_SIZE);
	space = min(space, WRITE_SIZE);

	// only get the new stream information on first call so we can reset decode.direct appropriately
	if (ctx->decode.new_stream) {
		space = 0;
	}

	ret = MPG123(&m, decode, ctx->decode.handle, ctx->streambuf->readp, bytes, write_buf, space, &size);

	if (ret == MPG123_NEW_FORMAT) {

		if (ctx->decode.new_stream) {
			long rate;
			int channels, enc;

			MPG123(&m, getformat, ctx->decode.handle, &rate, &channels, &enc);

			LOG_INFO("[%p]: setting track_start", ctx);
			LOCK_O_not_direct;
			ctx->output.current_sample_rate = decode_newstream(rate, ctx->output.supported_rates, ctx);
			ctx->output.track_start = ctx->outputbuf->writep;
			if (ctx->output.fade_mode) _checkfade(true, ctx);
			ctx->decode.new_stream = false;
			UNLOCK_O_not_direct;

		} else {
			LOG_WARN("[%p]: format change mid stream - not supported", ctx);
		}
	}

	_buf_inc_readp(ctx->streambuf, bytes);

	IF_DIRECT(
		_buf_inc_writep(ctx->outputbuf, size);
	);
	IF_PROCESS(
		ctx->process.in_frames = size / BYTES_PER_FRAME;
	);

	UNLOCK_O_direct;

	LOG_SDEBUG("[%p]: write %u frames", size / BYTES_PER_FRAME, ctx);

	if (ret == MPG123_DONE || (bytes == 0 && size == 0 && ctx->stream.state <= DISCONNECT)) {
		UNLOCK_S;
		LOG_INFO("[%p]: stream complete", ctx);
		return DECODE_COMPLETE;
	}

	UNLOCK_S;

	if (ret == MPG123_ERR) {
		LOG_WARN("[%p]: Error", ctx);
		return DECODE_COMPLETE;
	}

	// OK and NEED_MORE keep running
	return DECODE_RUNNING;
}

static void mpg_open(u8_t sample_size, u32_t sample_rate, u8_t channels, u8_t endianness, struct thread_ctx_s *ctx) {
	int err;

	if (ctx->decode.handle) {
		MPG123(&m, delete, ctx->decode.handle);
	}

	ctx->decode.handle = MPG123(&m, new, NULL, &err);

	if (ctx->decode.handle == NULL) {
		LOG_WARN("[%p]: new error: %s", ctx, MPG123(&m, plain_strerror, err));
	}


	//MPG123(&m, rates, &list, &count);
	MPG123(&m, format_none, ctx->decode.handle);
	// this decoder has a problem with re-sync of live streams
	//MPG123(&m, param, ctx->decode.handle, MPG123_FORCE_RATE, 44100, 0);
	//MPG123(&m, param, ctx->decode.handle, MPG123_REMOVE_FLAGS, MPG123_GAPLESS, 0);

	// restrict output to 44100, 16bits signed 2 channel based on library capability
	MPG123(&m, format, ctx->decode.handle, 44100, 2, MPG123_ENC_SIGNED_16);
	/*
	for (i = 0; i < count; i++) {
		MPG123(&m, format, ctx->decode.handle, list[i], 2, MPG123_ENC_SIGNED_16);
	}
	*/

	err = MPG123(&m, open_feed, ctx->decode.handle);

	if (err) {
		LOG_WARN("[%p]: open feed error: %s", ctx, MPG123(&m, plain_strerror, err));
	}
}

static void mpg_close(struct thread_ctx_s *ctx) {
	MPG123(&m, delete, ctx->decode.handle);
	ctx->decode.handle = NULL;
}

static bool load_mpg(void) {
#if !LINKALL
	void *handle = dlopen(LIBMPG, RTLD_NOW);
	char *err;

	if (!handle) {
		LOG_INFO("dlerror: %s", dlerror());
		return false;
	}
	
	m.mpg123_init = dlsym(handle, "mpg123_init");
	m.mpg123_feature = dlsym(handle, "mpg123_feature");
	m.mpg123_rates = dlsym(handle, "mpg123_rates");
	m.mpg123_format_none = dlsym(handle, "mpg123_format_none");
	m.mpg123_format = dlsym(handle, "mpg123_format");
	m.mpg123_param = dlsym(handle, "mpg123_param");
	m.mpg123_getparam = dlsym(handle, "mpg123_getparam");
	m.mpg123_new = dlsym(handle, "mpg123_new");
	m.mpg123_delete = dlsym(handle, "mpg123_delete");
	m.mpg123_open_feed = dlsym(handle, "mpg123_open_feed");
	m.mpg123_decode = dlsym(handle, "mpg123_decode");
	m.mpg123_getformat = dlsym(handle, "mpg123_getformat");
	m.mpg123_plain_strerror = dlsym(handle, "mpg123_plain_strerror");

	if ((err = dlerror()) != NULL) {
		LOG_INFO("dlerror: %s", err);		
		return false;
	}

	LOG_INFO("loaded "LIBMPG, NULL);
#endif

	return true;
}

struct codec *register_mpg(void) {
	static struct codec ret = { 
		'm',          // id
		"mp3",        // types
		READ_SIZE,    // min read
		WRITE_SIZE,   // min space
		mpg_open,     // open
		mpg_close,    // close
		mpg_decode,   // decode
	};

	if (!load_mpg()) {
		return NULL;
	}

	MPG123(&m, init);

	LOG_INFO("using mpg to decode mp3", NULL);
	return &ret;
}
