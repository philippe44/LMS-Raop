/*
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *  (c) Philippe, philippe_44@outlook.com for raop/multi-instance modifications
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

extern log_level decode_loglevel;
static log_level *loglevel = &decode_loglevel;

#define LOCK_S   mutex_lock(ctx->streambuf->mutex)
#define UNLOCK_S mutex_unlock(ctx->streambuf->mutex)
#define LOCK_O   mutex_lock(ctx->outputbuf->mutex)
#define UNLOCK_O mutex_unlock(ctx->outputbuf->mutex)
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

#define MAX_DECODE_FRAMES 4096

struct pcm {
	u32_t sample_rate;
	u8_t sample_size;
	u8_t channels;
	bool big_endian;
	bool  limit;
	u32_t audio_left;
};

typedef enum { UNKNOWN = 0, WAVE, AIFF } header_format;

/*---------------------------------------------------------------------------*/
static void check_header(struct thread_ctx_s *ctx) {
	u8_t *ptr = ctx->streambuf->readp;
	struct pcm *p = ctx->decode.handle;
	unsigned bytes = min(_buf_used(ctx->streambuf), _buf_cont_read(ctx->streambuf));
	header_format format = UNKNOWN;

	// simple parsing of wav and aiff headers and get to samples

	if (bytes > 12) {
		if (!memcmp(ptr, "RIFF", 4) && !memcmp(ptr+8, "WAVE", 4)) {
			LOG_INFO("[%p]: WAVE", ctx);
			format = WAVE;
		} else if (!memcmp(ptr, "FORM", 4) && (!memcmp(ptr+8, "AIFF", 4) || !memcmp(ptr+8, "AIFC", 4))) {
			LOG_INFO("[%p]: AIFF", ctx);
			format = AIFF;
		}
	}

	if (format != UNKNOWN) {
		ptr   += 12;
		bytes -= 12;

		while (bytes >= 8) {
			char id[5];
			unsigned len;
			memcpy(id, ptr, 4);
			id[4] = '\0';

			if (format == WAVE) {
				len = *(ptr+4) | *(ptr+5) << 8 | *(ptr+6) << 16| *(ptr+7) << 24;
			} else {
				len = *(ptr+4) << 24 | *(ptr+5) << 16 | *(ptr+6) << 8 | *(ptr+7);
			}

			LOG_INFO("header: %s len: %d", id, len);

			if (format == WAVE && !memcmp(ptr, "data", 4)) {
				ptr += 8;
				_buf_inc_readp(ctx->streambuf, ptr - ctx->streambuf->readp);
				p->audio_left = len;

				if ((p->audio_left == 0xFFFFFFFF) || (p->audio_left == 0x7FFFEFFC)) {
					LOG_INFO("[%p]: wav audio size unknown: %u", ctx, p->audio_left);
					p->limit = false;
				} else {
					LOG_INFO("[%p]: wav audio size: %u", ctx, p->audio_left);
					p->limit = true;
				}
				return;
			}

			if (format == AIFF && !memcmp(ptr, "SSND", 4) && bytes >= 16) {
				unsigned offset = *(ptr+8) << 24 | *(ptr+9) << 16 | *(ptr+10) << 8 | *(ptr+11);
				// following 4 bytes is blocksize - ignored
				ptr += 8 + 8;
				_buf_inc_readp(ctx->streambuf, ptr + offset - ctx->streambuf->readp);

				// Reading from an upsampled stream, length could be wrong.
				// Only use length in header for files.
				if (ctx->stream.state == STREAMING_FILE) {
					p->audio_left = len - 8 - offset;
					LOG_INFO("[%p]: aif audio size: %u", ctx, p->audio_left);
					p->limit = true;
				}
				return;
			}

			if (format == WAVE && !memcmp(ptr, "fmt ", 4) && bytes >= 24) {
				// override the server parsed values with our own
				p->channels    = *(ptr+10) | *(ptr+11) << 8;
				p->sample_rate = *(ptr+12) | *(ptr+13) << 8 | *(ptr+14) << 16 | *(ptr+15) << 24;
				p->sample_size = (*(ptr+22) | *(ptr+23) << 8);
				p->big_endian   = false;
				LOG_INFO("[%p]: pcm size: %u rate: %u chan: %u bigendian: %u", ctx, p->sample_size, p->sample_rate, p->channels, p->big_endian);
			}

			if (format == AIFF && !memcmp(ptr, "COMM", 4) && bytes >= 26) {
				int exponent;
				// override the server parsed values with our own
				p->channels    = *(ptr+8) << 8 | *(ptr+9);
				p->sample_size = (*(ptr+14) << 8 | *(ptr+15));
				p->big_endian   = true;
				// sample rate is encoded as IEEE 80 bit extended format
				// make some assumptions to simplify processing - only use first 32 bits of mantissa
				exponent = ((*(ptr+16) & 0x7f) << 8 | *(ptr+17)) - 16383 - 31;
				p->sample_rate  = *(ptr+18) << 24 | *(ptr+19) << 16 | *(ptr+20) << 8 | *(ptr+21);
				while (exponent < 0) { p->sample_rate >>= 1; ++exponent; }
				while (exponent > 0) { p->sample_rate <<= 1; --exponent; }
				LOG_INFO("[%p]: pcm size: %u rate: %u chan: %u bigendian: %u", ctx, p->sample_size, p->sample_rate, p->channels, p->big_endian);
			}

			if (bytes >= len + 8) {
				ptr   += len + 8;
				bytes -= (len + 8);
			} else {
				LOG_WARN("[%p]: run out of data", ctx);
				return;
			}
		}

	} else {
		LOG_WARN("[%p]: unknown format - can't parse header", ctx);
	}
}


/*---------------------------------------------------------------------------*/
decode_state pcm_decode(struct thread_ctx_s *ctx) {
	unsigned bytes, in, out, bytes_per_frame;
	frames_t frames;
	u8_t *iptr, *optr, buf[3*8];
	struct pcm *p = ctx->decode.handle;
	bool done = false;

	LOCK_S;
	if (ctx->decode.new_stream) check_header(ctx);
	LOCK_O_direct;

	iptr = (u8_t *)ctx->streambuf->readp;

	if (ctx->decode.new_stream && p->big_endian && !(*((u64_t*) iptr)) &&
		   (strstr(ctx->server_version, "7.7") || strstr(ctx->server_version, "7.8"))) {
		/*
		LMS < 7.9 does not remove 8 bytes when sending aiff files but it does
		when it is a transcoding ... so this does not matter for 16 bits samples
		but it is a mess for 24 bits ... so this tries to guess what we are
		receiving
		*/
		_buf_inc_readp(ctx->streambuf, 8);
		LOG_INFO("[%p]: guessing a AIFF extra header", ctx);
	}

	bytes = min(_buf_used(ctx->streambuf), _buf_cont_read(ctx->streambuf));
	bytes_per_frame = (p->sample_size * p->channels) / 8;

	if ((ctx->stream.state <= DISCONNECT && bytes == 0) || (p->limit && p->audio_left == 0)) {
		UNLOCK_O_direct;
		UNLOCK_S;
		return DECODE_COMPLETE;
	}

	in = bytes / bytes_per_frame;

	IF_DIRECT(
		out = min(_buf_space(ctx->outputbuf), _buf_cont_write(ctx->outputbuf)) / BYTES_PER_FRAME;
		optr = ctx->outputbuf->writep;
	);
	IF_PROCESS(
		out = ctx->process.max_in_frames;
		optr = ctx->process.inbuf;
	);

	if (ctx->decode.new_stream) {
		LOG_INFO("[%p]: setting track_start", ctx);

		LOCK_O_not_direct;
		//FIXME: not in use for now, sample rate always same how to know starting rate when resamplign will be used
		ctx->output.current_sample_rate = decode_newstream(p->sample_rate, ctx->output.supported_rates, ctx);
		ctx->output.track_start = ctx->outputbuf->writep;
		if (ctx->output.fade_mode) _checkfade(true, ctx);
		ctx->decode.new_stream = false;
		UNLOCK_O_not_direct;
		IF_PROCESS(
			out = ctx->process.max_in_frames;
		);
	}

	if (in == 0 && bytes > 0 && _buf_used(ctx->streambuf) >= bytes_per_frame) {
		memcpy(buf, iptr, bytes);
		memcpy(buf + bytes, ctx->streambuf->buf, bytes_per_frame - bytes);
		iptr = buf;
		in = 1;
	}

	frames = min(in, out);
	frames = min(frames, MAX_DECODE_FRAMES);

	if (p->limit && frames * bytes_per_frame > p->audio_left) {
		LOG_INFO("[%p]: reached end of audio", ctx);
		frames = p->audio_left / bytes_per_frame;
	}

	// stereo, 16 bits
	if (p->sample_size == 16 && p->channels == 2) {
		done = true;
		if (!p->big_endian) memcpy(optr, iptr, frames * BYTES_PER_FRAME);
		else {
			// 4 bytes per frames, 1/2 frame(1 channel) at each loop
			int count = frames * 2;
			while (count--) {
				*optr++ = *(iptr + 1);
				*optr++ = *iptr;
				iptr += 2;
			}
		}
	}

	// mono, 16 bits
	if (p->sample_size == 16 && p->channels == 1) {
		int count = frames;

		done = true;
		// 2 bytes per sample and mono, expand to stereo, 1 frame per loop
		if (!p->big_endian) {
			while (count--) {
				*optr++ = *iptr;
				*optr++ = *(iptr + 1);
				*optr++ = *iptr++;
				*optr++ = *iptr++;
			}
		}
		else {
			while (count--) {
				*optr++ = *(iptr + 1);
				*optr++ = *iptr;
				*optr++ = *(iptr + 1);
				*optr++ = *iptr++;
				iptr++;
			}
		}
	}

	// 24 bits, the tricky one
	if (p->sample_size == 24 && p->channels == 2) {
		int count = frames * 2;

		done = true;
		// 3 bytes per sample, shrink to 2 and do 1/2 frame (1 channel) per loop
		if (!p->big_endian) {
			while (count--) {
				iptr++;
				*optr++ = *iptr++;
				*optr++ = *iptr++;
			}
		}
		else {
			while (count--) {
				*optr++ = *(iptr + 1);
				*optr++ = *iptr;
				iptr += 3;
			}
		}
	}

	_buf_inc_readp(ctx->streambuf, frames * bytes_per_frame);

	if (p->limit) {
		p->audio_left -= frames * bytes_per_frame;
	}

	IF_DIRECT(
		_buf_inc_writep(ctx->outputbuf, frames * BYTES_PER_FRAME);
	);
	IF_PROCESS(
		ctx->process.in_frames = frames;
	);

	UNLOCK_O_direct;
	UNLOCK_S;

	if (!done && frames) {
		LOG_ERROR("[%p]: unhandled channel*bytes %d %d", p->sample_size, p->channels);
	}

	return DECODE_RUNNING;
}


/*---------------------------------------------------------------------------*/
static void pcm_open(u8_t sample_size, u32_t sample_rate, u8_t channels, u8_t endianness, struct thread_ctx_s *ctx) {
	struct pcm *p = ctx->decode.handle;

	if (!p)	p = ctx->decode.handle = malloc(sizeof(struct pcm));

	if (!p) return;

	p->sample_size = sample_size;
	p->sample_rate = sample_rate,
	p->channels = channels;
	p->big_endian = (endianness == 0);

	LOG_INFO("pcm size: %u rate: %u chan: %u bigendian: %u", sample_size, sample_rate, channels, endianness);
}


/*---------------------------------------------------------------------------*/
static void pcm_close(struct thread_ctx_s *ctx) {
	if (ctx->decode.handle) free(ctx->decode.handle);
	ctx->decode.handle = NULL;
}


/*---------------------------------------------------------------------------*/
struct codec *register_pcm(void) {
	static struct codec ret = { 
		'p',         // id
		"aif,wav,pcm",   // types
		4096,        // min read
		102400,      // min space
		pcm_open,    // open
		pcm_close,   // close
		pcm_decode,  // decode
	};

	LOG_INFO("using pcm to decode aif,pcm", NULL);
	return &ret;
}


void deregister_pcm(void) {
}



