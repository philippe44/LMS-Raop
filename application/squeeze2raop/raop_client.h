/*****************************************************************************
 * rtsp_client.h: RAOP Client
 *
 * Copyright (C) 2004 Shiro Ninomiya <shiron@snino.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/
#ifndef __RAOP_CLIENT_H_
#define __RAOP_CLIENT_H_

#include "platform.h"

#define BUFFER 512 //max frames airport can receive
#define MAX_SAMPLES_IN_CHUNK 352

typedef struct raopcl_t {__u32 dummy;} raopcl_t;

struct raopcl_s;

#define RAOP_NOPARAM (-1)

typedef enum raop_codec_s { RAOP_PCM = 0, RAOP_ALAC, RAOP_AAC,
							RAOP_AAL_ELC } raop_codec_t;

typedef enum raop_cypto_s { RAOP_CLEAR = 0, RAOP_RSA, RAOP_FAIRPLAY, RAOP_MFISAP,
							RAOP_FAIRPLAYSAP } raop_crypto_t;

typedef enum raop_states_s { RAOP_DOWN_FULL = 0, RAOP_PEER_DISCONNECT, RAOP_DOWN,
							 RAOP_FLUSHING, RAOP_FLUSHED, RAOP_STREAMING } raop_state_t;

typedef enum raop_flush_s { RAOP_FLUSH, RAOP_RECLOCK, RAOP_REBUFFER } raop_flush_t;


typedef struct {
	int channels;
	int	sample_size;
	int	sample_rate;
	raop_codec_t codec;
	raop_crypto_t crypto;
} raop_settings_t;

typedef struct {
	__u8 proto;
	__u8 type;
	__u8 seq[2];
	__u32 timestamp;
#if WIN
} rtp_header_t;
#else
} __attribute__ ((packed)) rtp_header_t;
#endif

typedef struct {
	rtp_header_t hdr;
	ntp_t   curr_time;
	__u32   rtp_timestamp;
#if WIN
} rtp_sync_pkt_t;
#else
} __attribute__ ((packed)) rtp_sync_pkt_t;
#endif

typedef struct {
	rtp_header_t hdr;
	__u32 ssrc;
#if WIN
} rtp_audio_pkt_t;
#else
} __attribute__ ((packed)) rtp_audio_pkt_t;
#endif

struct raopcl_s *raopcl_create(char *local, raop_codec_t codec, raop_crypto_t crypto, int sample_rate, int sample_size, int channels, int volume);
bool	raopcl_destroy(struct raopcl_s *p);
bool	raopcl_connect(struct raopcl_s *p, struct in_addr host, __u16 destport, raop_codec_t codec);
bool    raopcl_flush_stream(struct raopcl_s *p, raop_flush_t mode);
bool 	raopcl_start_stream(struct raopcl_s *p);
bool 	raopcl_disconnect(struct raopcl_s *p);
bool 	raopcl_teardown(struct raopcl_s *p);
bool 	raopcl_close(struct raopcl_s *p);
bool 	raopcl_update_volume(struct raopcl_s *p, int vol, bool force);
__u32	raopcl_send_sample(struct raopcl_s *p, __u8 *sample, int size, int frames, bool skip, int read_ahead);
bool 	raopcl_set_content(raopcl_t *p, char* itemname, char* songartist, char* songalbum);
__u32 	raopcl_get_timestamp(struct raopcl_s *p, __u32 origin_ms, __u32 count);
u32_t 	raopcl_get_latency(struct raopcl_s *p);

int raopcl_wait_songdone(raopcl_t *p, int set);
int raopcl_pause_check(raopcl_t *p);
int raopcl_aexbuf_time(raopcl_t *p, struct timeval *dtv);



#endif
