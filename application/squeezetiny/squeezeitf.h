/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2014, triode1@btinternet.com
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

#ifndef __SQUEEZEITF_H
#define __SQUEEZEITF_H

#include "squeezedefs.h"
#include "util_common.h"

#define SQ_STR_LENGTH	256

typedef enum { SQ_PLAY, SQ_PAUSE, SQ_UNPAUSE, SQ_STOP, SQ_SEEK,
			  SQ_VOLUME, SQ_ONOFF, SQ_NEXT, SQ_CONNECT, SQ_STARTED,
			  SQ_METASEND } sq_action_t;
typedef enum {SQ_STREAM = 2, SQ_FULL = 3} sq_mode_t;
typedef	sq_action_t sq_event_t;

//#define MAX_SUPPORTED_SAMPLERATES 16
#define MAX_SUPPORTED_SAMPLERATES 2
#define TEST_RATES = { 384000, 352000, 192000, 176400, 96000, 88200, 48000, 44100, 32000, 24000, 22500, 16000, 12000, 11025, 8000, 0 }

#define STREAMBUF_SIZE (2 * 1024 * 1024)
#define OUTPUTBUF_SIZE (44100 * 4 * 10)

typedef enum { SQ_RATE_384000 = 384000, SQ_RATE_352000 = 352000,
			   SQ_RATE_192000 = 192000, SQ_RATE_176400 = 176400,
			   SQ_RATE_96000 = 96000, SQ_RATE_48000 = 48000, SQ_RATE_44100 = 44100,
			   SQ_RATE_32000 = 32000, SQ_RATE_24000 = 24000, SQ_RATE_22500 = 22500,
			   SQ_RATE_16000 = 16000, SQ_RATE_12000 = 12000, SQ_RATE_11025 = 11025,
			   SQ_RATE_8000 = 8000, SQ_RATE_DEFAULT = 0} sq_rate_e;
typedef enum { L24_PACKED, L24_PACKED_LPCM, L24_TRUNC_16, L24_TRUNC_16_PCM, L24_UNPACKED_HIGH, L24_UNPACKED_LOW } sq_L24_pack_t;
typedef enum { FLAC_NO_HEADER = 0, FLAC_NORMAL_HEADER = 1, FLAC_FULL_HEADER = 2 } sq_flac_header_t;
typedef	int	sq_dev_handle_t;
typedef unsigned sq_rate_t;

typedef struct sq_metadata_s {
	char *artist;
	char *album;
	char *title;
	char *genre;
	char *path;
	char *artwork;
	u32_t index;
	u32_t track;
	u32_t duration;
	u32_t file_size;
	bool  remote;
	u32_t track_hash;
} sq_metadata_t;

typedef	struct sq_dev_param_s {
	unsigned 	stream_buf_size;
	unsigned 	output_buf_size;
	char		codecs[SQ_STR_LENGTH];
	u32_t		sample_rate;
	u8_t		mac[6];
	signed char	player_volume;
	u16_t		read_ahead;
#if defined(RESAMPLE)
	bool		resample;
	char		resample_options[SQ_STR_LENGTH];
#endif
} sq_dev_param_t;


typedef struct
{
	u8_t	channels;
	u8_t	sample_size;
	u32_t	sample_rate;
	u8_t	codec;
	u8_t 	endianness;
} sq_format_t;


struct raopcl_s;

typedef bool (*sq_callback_t)(sq_dev_handle_t handle, void *caller_id, sq_action_t action, u8_t *cookie, void *param);

void				sq_init(char *server);
void				sq_end(void);

// only name cannot be NULL
bool			 	sq_run_device(sq_dev_handle_t handle, struct raopcl_s *raopcl,
								  char *name, sq_dev_param_t *param,
								  u32_t sample_rate, u8_t sample_size);
void				sq_delete_device(sq_dev_handle_t);
sq_dev_handle_t		sq_reserve_device(void *caller_id, sq_callback_t callback);
void				sq_release_device(sq_dev_handle_t);

bool				sq_call(sq_dev_handle_t handle, sq_action_t action, void *param);
void				sq_notify(sq_dev_handle_t handle, void *caller_id, sq_event_t event, u8_t *cookie, void *param);
bool				sq_get_metadata(sq_dev_handle_t handle, struct sq_metadata_s *metadata, bool next);
void				sq_default_metadata(struct sq_metadata_s *metadata, bool init);
void 				sq_free_metadata(struct sq_metadata_s *metadata);

#endif

