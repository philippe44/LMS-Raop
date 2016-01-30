/*
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
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


/*---------------------------------------------------------------------------*/
decode_state pcm_decode(struct thread_ctx_s *ctx) {
	unsigned bytes, space;
	u8_t *iptr, *optr;

	LOCK_S;
	LOCK_O_direct;

	bytes = min(_buf_used(ctx->streambuf), _buf_cont_read(ctx->streambuf));

	/*
	It is required to use min with buf_space as it is the full space - 1,
	otherwise, a write to full would be authorized and the write pointer
	would wrap to the read pointer, making impossible to know if the buffer
	is full or empty. This as the consequence, though, that the buffer can
	never be totally full and can only wrap once the read pointer has moved
	so it is impossible to count on having a proper multiply of any number
	of bytes in the buffer
	*/
	space = min(_buf_cont_read(ctx->streambuf), _buf_cont_write(ctx->outputbuf));

	IF_DIRECT(
		space = min(space, _buf_space(ctx->outputbuf));
		optr = ctx->outputbuf->writep;
	);
	IF_PROCESS(
		//FIXME : to be confirmed
		space = min(space, ctx->process.max_in_frames);
		optr = ctx->process.inbuf;
	);

	iptr = (u8_t *)ctx->streambuf->readp;

	if (ctx->stream.state <= DISCONNECT && bytes == 0) {
		UNLOCK_O_direct;
		UNLOCK_S;
		return DECODE_COMPLETE;
	}

	if (ctx->decode.new_stream) {
		LOG_INFO("[%p]: setting track_start", ctx);

		/*
		LMS < 7.9 does not remove 8 bytes when sending aiff files but it does
		when it is a transcoding ... so this does not matter for 16 bits samples
		but it is a mess for 24 bits ... so this tries to guess what we are
		receiving
		*/
		if (ctx->decode.big_endian && !(*((u64_t*) iptr)) &&
		   (strstr(ctx->server_version, "7.7") || strstr(ctx->server_version, "7.8"))) {
			iptr += 8;
			space -= 8;
			_buf_inc_readp(ctx->streambuf, 8);
			LOG_INFO("[%p]: guessing a AIFF extra header", ctx);
		}

		LOCK_O_not_direct;
		//FIXME: not in use for now, sample rate always same how to know starting rate when resamplign will be used
		//output.next_sample_rate = decode_newstream(sample_rate, output.supported_rates);
		ctx->output.track_start = ctx->outputbuf->writep;
		if (ctx->output.fade_mode) _checkfade(true, ctx);
		ctx->decode.new_stream = false;
		UNLOCK_O_not_direct;
		IF_PROCESS(
			space = ctx->process.max_in_frames;
		);
	}

	if (ctx->decode.sample_size == 16 && ctx->decode.channels == 2) {
		if (!ctx->decode.big_endian) memcpy(optr, iptr, space);
		else {
			int count = space / 2;
			while (count--) {
				*optr++ = *(iptr + 1);
				*optr++ = *iptr;
				iptr += 2;
			}
		}

		_buf_inc_readp(ctx->streambuf, space);
		_buf_inc_writep(ctx->outputbuf, space);
	}

	/*
	Single channel, this will be made in 2 turns and space is not a multiple of
	2 in streambuf, but it must be in output buf, so we might not be complete
	because of output but next turn will take care of that as we'll move back
	to the beginning of streambuf
	*/
	if (ctx->decode.sample_size == 16 && ctx->decode.channels == 1) {
		int i, count = space / 4;

		i = count;
		if (!ctx->decode.big_endian)
			while (i--) {
				*optr++ = *iptr;
				*optr++ = *(iptr + 1);
				*optr++ = *iptr++;
				*optr++ = *iptr++;
			}
		else
			while (i--) {
				*optr++ = *(iptr + 1);
				*optr++ = *iptr;
				*optr++ = *(iptr + 1);
				*optr++ = *iptr++;
				iptr++;
			}

		_buf_inc_readp(ctx->streambuf, count);
		_buf_inc_writep(ctx->outputbuf, count * 2);
	}

	/*
	24 bits - space is not a multiple of 3 in output, but it must be in
	streambuf, so if we are not complete because of output, next turn
	will finish the job as we'll move back to the begining of output
	*/
	if (ctx->decode.sample_size == 24) {
		int i, count = space / 3;
		u8_t buf[3];

		// workaround the buffer wrap in case space = 1 or 2
		if (!count && space && _buf_used(ctx->streambuf) >= 3 && _buf_cont_write(ctx->outputbuf) >= 2) {
			memcpy(buf, ctx->streambuf->readp, space);
			memcpy(buf + space, ctx->streambuf->buf, 3 - space);
			iptr = buf;
			count = 1;
		}

		i = count;
		if (!ctx->decode.big_endian)
			while (i--) {
				iptr++;
				*optr++ = *iptr++;
				*optr++ = *iptr++;
			}
		else
			while (i--) {
				*optr++ = *(iptr + 1);
				*optr++ = *iptr;
				iptr += 3;
			}

		_buf_inc_readp(ctx->streambuf, count * 3);
		_buf_inc_writep(ctx->outputbuf, count * 2);
	}

	UNLOCK_O_direct;
	UNLOCK_S;

	return DECODE_RUNNING;
}


/*---------------------------------------------------------------------------*/
static void pcm_open(u8_t sample_size, u32_t sample_rate, u8_t channels, u8_t endianness, struct thread_ctx_s *ctx) {
	LOG_INFO("pcm size: %u rate: %u chan: %u bigendian: %u", sample_size, sample_rate, channels, endianness);
}


/*---------------------------------------------------------------------------*/
static void pcm_close(struct thread_ctx_s *ctx) {
}


/*---------------------------------------------------------------------------*/
struct codec *register_pcm(void) {
	static struct codec ret = { 
		'p',         // id
		"aif,pcm",   // types
		4096,        // min read
		102400,      // min space
		pcm_open,    // open
		pcm_close,   // close
		pcm_decode,  // decode
	};

	LOG_INFO("using pcm to decode aif,pcm", NULL);
	return &ret;
}

/*---------------------------------------------------------------------------*/
#if 0
static void *decode_thread(struct thread_ctx_s *ctx) {
	while (ctx->decode_running) {
		size_t bytes, space;
		bool toend;
		bool ran = false;

		LOCK_S;LOCK_O;
		bytes = _buf_used(ctx->streambuf);
		space = min(_buf_cont_read(ctx->streambuf), _buf_cont_write(ctx->outputbuf));
		/*
		It is required to use min with buf_space as it is the full space - 1,
		otherwise, a write to full would be authorized and the write pointer
		would wrap to the read pointer, making impossible to know if the buffer
		is full or empty. This as the consequence, though, that the buffer can
		never be totally full and can only wrap once the read pointer has moved
		so it is impossible to count on having a proper multiply of any number
		of bytes in the buffer
		*/
		space = min(space, _buf_space(ctx->outputbuf));
		toend = (ctx->stream.state <= DISCONNECT);
		UNLOCK_S;UNLOCK_O;

		LOCK_D;

		if (ctx->decode.state == DECODE_RUNNING) {

			if (ctx->decode.new_stream) {
				LOG_INFO("[%p]: setting track_start", ctx);
				LOCK_O;
				ctx->output.track_start = ctx->outputbuf->writep;
				UNLOCK_O;
				if (ctx->output.fade_mode) _checkfade(true, ctx);
				ctx->decode.new_stream = false;
			}

			LOCK_S;LOCK_O;
			if (ctx->decode.sample_size == 16 && ctx->decode.channels == 2) {
				memcpy(ctx->outputbuf->writep, ctx->streambuf->readp, space);
				_buf_inc_readp(ctx->streambuf, space);
				_buf_inc_writep(ctx->outputbuf, space);
			}

			/*
			Single channel, space is not a multiple of 2 in streambuf, but it
			must be in output buf, so we might not be complete because of output
			but next turn will take care of that as we'll move back to the
			beginning of streambuf
			*/
			if (ctx->decode.sample_size == 16 && ctx->decode.channels == 1) {
				int i, count = space / 2;
				u8_t *src = ctx->streambuf->readp;
				u8_t *dst = ctx->outputbuf->writep;

				i = count;
				while (i--) {
					*dst++ = *src;
					*dst++ = *src++;
				}

				_buf_inc_readp(ctx->streambuf, count);
				_buf_inc_writep(ctx->outputbuf, count * 2);
			}

			/*
			24 bits - space is not a multiple of 3 in output, but it must be in
			streambuf, so if we are not complete because of output, next turn
			will finish the job as we'll move back to the begining of output
			*/
			if (ctx->decode.sample_size == 24) {
				int i, count = space / 3;
				u8_t buf[3];
				u8_t *src;
				u8_t *dst = ctx->outputbuf->writep;

				// workaround the buffer wrap in case space = 1 or 2
				if (!count && space && _buf_used(ctx->streambuf) >= 3 && _buf_cont_write(ctx->outputbuf) > 2) {
					memcpy(buf, ctx->streambuf->readp, space);
					memcpy(buf + space, ctx->streambuf->buf, 3 - space);
					src = buf;
					count = 1;
				}
				else src = ctx->streambuf->readp;

				i = count;
				while (i--) {
					src++;
					*dst++ = *src++;
					*dst++ = *src++;
				}

				_buf_inc_readp(ctx->streambuf, count * 3);
				_buf_inc_writep(ctx->outputbuf, count * 2);
			}

			UNLOCK_S;UNLOCK_O;

			if (toend) {

				if (!bytes ) ctx->decode.state = DECODE_COMPLETE;

				if (ctx->decode.state != DECODE_RUNNING) {

					LOG_INFO("decode %s", ctx->decode.state == DECODE_COMPLETE ? "complete" : "error");

					if (ctx->output.fade_mode) _checkfade(false, ctx);

					wake_controller(ctx);
				}

				ran = true;
			}
		}

		UNLOCK_D;

		if (!ran) {
			usleep(100000);
		}
	}

	return 0;
}
#endif
