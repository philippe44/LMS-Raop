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

// raop output

#include "squeezelite.h"
#include "raop_client.h"
#include "alac_wrapper.h"

#define FRAME_BLOCK MAX_SILENCE_FRAMES

extern log_level	output_loglevel;
static log_level 	*loglevel = &output_loglevel;

#define LOCK   mutex_lock(ctx->outputbuf->mutex)
#define UNLOCK mutex_unlock(ctx->outputbuf->mutex)
#define LOCK_S   mutex_lock(ctx->streambuf->mutex)
#define UNLOCK_S mutex_unlock(ctx->streambuf->mutex)


/*---------------------------------------------------------------------------*/
void wake_output(struct thread_ctx_s *ctx) {
	return;
}


/*---------------------------------------------------------------------------*/
static int _raop_write_frames(struct thread_ctx_s *ctx, frames_t out_frames, bool silence, s32_t gainL, s32_t gainR,
								s32_t cross_gain_in, s32_t cross_gain_out, s16_t **cross_ptr) {

	s16_t *obuf;

	if (!silence) {

		if (ctx->output.fade == FADE_ACTIVE && ctx->output.fade_dir == FADE_CROSS && *cross_ptr) {
			_apply_cross(ctx->outputbuf, out_frames, cross_gain_in, cross_gain_out, cross_ptr);
		}

		obuf = (s16_t*) ctx->outputbuf->readp;

	} else {

		obuf = (s16_t*) ctx->silencebuf;
	}

	_scale_frames((s16_t*) (ctx->output.buf + ctx->output.buf_frames * BYTES_PER_FRAME), obuf, out_frames, gainL, gainR);

	ctx->output.buf_frames += out_frames;

	return (int) out_frames;
}


/*---------------------------------------------------------------------------*/
void output_close(struct thread_ctx_s *ctx)
{
	output_close_common(ctx);
	free(ctx->output.buf);
	free(ctx->output.timerefs);
}


/*---------------------------------------------------------------------------*/
static void *output_raop_thread(struct thread_ctx_s *ctx) {
	while (ctx->output_running) {
		u8_t *buffer;
		int size;
		u32_t playtime;

		LOCK;
		// this will internally loop till we have exactly 352 frames
		_output_frames(FRAME_BLOCK, ctx);
		UNLOCK;

		// nothing to do, take a nap
		if (!ctx->output.buf_frames) {
			usleep(10000);
			continue;
		}

		if (ctx->output.state == OUTPUT_RUNNING) {
			pcm_to_alac_fast((u32_t*) ctx->output.buf, ctx->output.buf_frames, &buffer, &size, FRAME_BLOCK);
			playtime = raopcl_send_sample(ctx->output.device, buffer, size,
									 FRAME_BLOCK, false, ctx->config.read_ahead);
			NFREE(buffer);
		}
		else {
			playtime = raopcl_send_sample(ctx->output.device, NULL, 0, FRAME_BLOCK, true, 20);
        }

		ctx->output.buf_frames = 0;

		LOCK;
		ctx->output.updated = gettime_ms();
		ctx->output.timerefs[(playtime / TIMEGAPS) % ctx->output.nb_timerefs] = ctx->output.frames_played;
		ctx->output.frames_played_dmp = ctx->output.timerefs[(ctx->output.updated / TIMEGAPS) % ctx->output.nb_timerefs];

		// detect track start and memorize exact timestamp;
		switch (ctx->output.start_detect) {
		case DETECT_IDLE: break;
		case DETECT_ACQUIRE:
			ctx->output.track_start_time = playtime;
			ctx->output.start_detect = DETECT_STARTED;
			LOG_INFO("[%p]: track start time: %u", ctx, playtime);
			break;
		case DETECT_STARTED:
			if (ctx->output.updated >= ctx->output.track_start_time) {
				ctx->output.track_started = true;
				ctx->output.start_detect = DETECT_IDLE;
				LOG_INFO("[%p]: track started at: %u", ctx, ctx->output.updated);
			}
			break;
		default: break;
		}
		UNLOCK;
	}

	return 0;
}


/*---------------------------------------------------------------------------*/
void output_raop_thread_init(struct raopcl_s *raopcl, unsigned output_buf_size, u32_t sample_rate, u8_t sample_size, struct thread_ctx_s *ctx) {
	pthread_attr_t attr;

	LOG_INFO("[%p]: init output raop", ctx);

	memset(&ctx->output, 0, sizeof(ctx->output));

	ctx->output.buf = malloc(FRAME_BLOCK * BYTES_PER_FRAME);
	if (!ctx->output.buf) {
		LOG_ERROR("[%p]: unable to malloc buf", ctx);
		return;
	}

	ctx->output.nb_timerefs = (((u64_t) (ctx->config.read_ahead + 100) * sample_rate) / 1000) / TIMEGAPS + 1;
	ctx->output.timerefs = malloc(sizeof(ctx->output.timerefs) * ctx->output.nb_timerefs);
	if (!ctx->output.timerefs) {
		LOG_ERROR("[%p]: unable to malloc timerefs", ctx);
		exit(0);
	}
	memset(ctx->output.timerefs, 0, sizeof(ctx->output.timerefs) * ctx->output.nb_timerefs);

    ctx->output_running = true;
	ctx->output.buf_frames = 0;
	ctx->output.start_frames = FRAME_BLOCK * 2;
	ctx->output.write_cb = &_raop_write_frames;

	output_init_common(raopcl, output_buf_size, sample_rate, ctx);

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + OUTPUT_THREAD_STACK_SIZE);
	pthread_create(&ctx->output_thread, &attr, (void *(*)(void*)) &output_raop_thread, ctx);
	pthread_attr_destroy(&attr);
}



