/*****************************************************************************
 * rtsp_client.c: RAOP Client
 *
 * Copyright (C) 2004 Shiro Ninomiya <shiron@snino.com>
 *  (c) Philippe, philippe_44@outlook.com: AirPlay V2 + simple library
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
#include <stdio.h>
#include "platform.h"
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/engine.h>

#include <pthread.h>
#include <semaphore.h>

#include <time.h>
#include <stdlib.h>

#include <limits.h>
#include "aexcl_lib.h"
#include "rtsp_client.h"
#include "raop_client.h"
#include "base64.h"
#include "aes.h"

#define MAX_BACKLOG 256

#define JACK_STATUS_DISCONNECTED 0
#define JACK_STATUS_CONNECTED 1

#define JACK_TYPE_ANALOG 0
#define JACK_TYPE_DIGITAL 1

#define VOLUME_MIN -30
#define VOLUME_MAX 0

// all the following must be 32-bits aligned

typedef struct {
	rtp_header_t hdr;
	__u32 dummy;
	ntp_t ref_time;
	ntp_t recv_time;
	ntp_t send_time;
#if WIN
} rtp_time_pkt_t;
#else
} __attribute__ ((packed)) rtp_time_pkt_t;
#endif

typedef struct {
	rtp_header_t hdr;
	__u16 seq_number;
	__u16 n;
#if WIN
} rtp_lost_pkt_t;
#else
} __attribute__ ((packed)) rtp_lost_pkt_t;
#endif

typedef struct raopcl_s {
	struct rtspcl_s *rtspcl;
	raop_state_t state;
	int sane;
	__u8 iv[16]; // initialization vector for aes-cbc
	__u8 nv[16]; // next vector for aes-cbc
	__u8 key[16]; // key for aes-cbc
	struct in_addr	host_addr, local_addr;
	__u16 rtsp_port;
	rtp_port_t	rtp_ports;
	struct {
		__u16 seq_number;
		__u32 timestamp;
		int	size;
		u8_t *buffer;
	} backlog[MAX_BACKLOG];
	int ajstatus;
	int ajtype;
	int volume;
	aes_context ctx;
	int size_in_aex;
	bool encrypt;
	struct {
		__u32	first, first_clock;
		__u32   latest;
		__u32	audio;
	} rtp_ts;
	__u16   seq_number;
	raop_flush_t flush_mode;
	unsigned long ssrc;
	int latency;
	pthread_t time_thread, ctrl_thread;
	pthread_mutex_t mutex;
	bool time_running, ctrl_running;
	int sample_rate, sample_size, channels;
	int playtime;
	raop_codec_t codec;
	raop_crypto_t crypto;
} raopcl_data_t;


extern log_level	raop_loglevel;
static log_level 	*loglevel = &raop_loglevel;

static void *rtp_timing_thread(void *args);
static void *rtp_control_thread(void *args);
static void raopcl_terminate_rtp(struct raopcl_s *p);
static void raopcl_send_sync(struct raopcl_s *raopcld, bool first);


/*----------------------------------------------------------------------------*/
raop_state_t raopcl_get_state(struct raopcl_s *p)
{
	if (!p) return RAOP_DOWN_FULL;

	return p->state;
}


/*----------------------------------------------------------------------------*/
bool raopcl_is_sane(struct raopcl_s *p)
{
	if (!rtspcl_is_connected(p->rtspcl) || p->sane >= 10) return false;
	return true;
}


/*----------------------------------------------------------------------------*/
u32_t raopcl_get_latency(struct raopcl_s *p)
{
	if (!p) return 0;

	return p->latency;
}


/*----------------------------------------------------------------------------*/
static int rsa_encrypt(__u8 *text, int len, __u8 *res)
{
	RSA *rsa;
	__u8 modules[256];
	__u8 exponent[8];
	int size;
	char n[] =
			"59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUtwC"
			"5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDR"
			"KSKv6kDqnw4UwPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuB"
			"OitnZ/bDzPHrTOZz0Dew0uowxf/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJ"
			"Q+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/UAaHqn9JdsBWLUEpVviYnh"
			"imNVvYFZeCXg/IdTQ+x4IRdiXNv5hEew==";
    char e[] = "AQAB";

	rsa = RSA_new();
	size = base64_decode(n, modules);
	rsa->n = BN_bin2bn(modules, size, NULL);
	size = base64_decode(e, exponent);
	rsa->e = BN_bin2bn(exponent, size, NULL);
	size = RSA_public_encrypt(len, text, res, rsa, RSA_PKCS1_OAEP_PADDING);
	RSA_free(rsa);

	return size;
}

/*----------------------------------------------------------------------------*/
static int raopcl_encrypt(raopcl_data_t *raopcld, __u8 *data, int size)
{
	__u8 *buf;
	//__u8 tmp[16];
	int i=0,j;
	memcpy(raopcld->nv,raopcld->iv,16);
	while(i+16<=size){
		buf=data+i;
		for(j=0;j<16;j++) buf[j] ^= raopcld->nv[j];
		aes_encrypt(&raopcld->ctx, buf, buf);
		memcpy(raopcld->nv,buf,16);
		i+=16;
	}
	if(i<size){
#if 0
		LOG_INFO("[%p]: a block less than 16 bytes(%d) is not encrypted", raopcld, size-i);
		memset(tmp,0,16);
		memcpy(tmp,data+i,size-i);
		for(j=0;j<16;j++) tmp[j] ^= raopcld->nv[j];
		aes_encrypt(&raopcld->ctx, tmp, tmp);
		memcpy(raopcld->nv,tmp,16);
		memcpy(data+i,tmp,16);
		i+=16;
#endif
	}
	return i;
}


/*----------------------------------------------------------------------------*/
__u32 raopcl_send_sample(struct raopcl_s *p, __u8 *sample, int size, int frames, bool skip, int read_ahead)
{
	struct sockaddr_in addr;
	u8_t *buffer;
	rtp_audio_pkt_t *packet;
	u32_t playtime, now;
	struct timeval timeout = { 0, 20*1000L };
	fd_set wfds;
	size_t n;
	bool first = false;

	// don't send anything if not ready, this might confuse the player !
	if (!p) return 0;

	pthread_mutex_lock(&p->mutex);

	// wait for async flush to be executed
	if (p->state == RAOP_FLUSHING) {
		LOG_INFO("[%p]: waiting for flushing to end", p);
	}
	while (p->state == RAOP_FLUSHING) {
		pthread_mutex_unlock(&p->mutex);
		usleep(10000);
		pthread_mutex_lock(&p->mutex);
	}

	// Send 1st synchro if needed
	if (p->state == RAOP_FLUSHED) {
		if (p->flush_mode == RAOP_RECLOCK) {
			p->rtp_ts.first_clock = gettime_ms();
			p->rtp_ts.first = p->rtp_ts.audio;
		}

		if (p->flush_mode == RAOP_REBUFFER) p->rtp_ts.first = p->rtp_ts.audio;

		first = true;
		p->state = RAOP_STREAMING;
		raopcl_send_sync(p, true);
		LOG_INFO("[%p]: Send 1st packet + synchro at:%u", p, p->rtp_ts.first);
	}

	p->rtp_ts.audio += frames;

	pthread_mutex_unlock(&p->mutex);

	playtime = p->rtp_ts.first_clock + ((__u32) (p->rtp_ts.audio - p->rtp_ts.first) * 1000LL) / p->sample_rate;
	LOG_SDEBUG("[%p]: sending audio ts:%u (played:%u) ", p, p->rtp_ts.audio, playtime);

	// wait till we can send that frame (enough buffer consumed)
	if (playtime > (now = gettime_ms()) + read_ahead) {
		u32_t sleep_time = min(read_ahead / 2, 100);

		if (sleep_time < 10) sleep_time = 10;
		LOG_SDEBUG("[%p]: waiting for buffer to empty %u %u", p, p->rtp_ts.audio - p->rtp_ts.first, now);
		usleep(sleep_time * 1000);
	}

	// really send data only if needed
	if (p->rtp_ports.audio.fd == -1 || p->state != RAOP_STREAMING || skip || now > playtime || !sample)
		return playtime;

	addr.sin_family = AF_INET;
	addr.sin_addr = p->host_addr;
	addr.sin_port = htons(p->rtp_ports.audio.rport);

	// add an extra header for repeat, one less alloc to be done
	if ((buffer = malloc(sizeof(rtp_header_t) + sizeof(rtp_audio_pkt_t) + size)) == NULL) {
		LOG_ERROR("[%p]: cannot allocate sample buffer", p);
		return playtime;
	}

	// only increment if we really send
	pthread_mutex_lock(&p->mutex);
	p->seq_number++;
	pthread_mutex_unlock(&p->mutex);

	// packet is after re-transmit header
	packet = (rtp_audio_pkt_t *) (buffer + sizeof(rtp_header_t));
	packet->hdr.proto = 0x80;
	packet->hdr.type = 0x60 | (first ? 0x80 : 0);
	// packet->hdr.type = 0x0a | (first ? 0x80 : 0);
	packet->hdr.seq[0] = (p->seq_number >> 8) & 0xff;
	packet->hdr.seq[1] = p->seq_number & 0xff;
	packet->timestamp = htonl(p->rtp_ts.audio);
	packet->ssrc = htonl(p->ssrc);

	memcpy((u8_t*) packet + sizeof(rtp_audio_pkt_t), sample, size);

	// with newer airport express, don't use encryption (??)
	if (p->encrypt) raopcl_encrypt(p, (u8_t*) packet + sizeof(rtp_audio_pkt_t), size);

	pthread_mutex_lock(&p->mutex);
	n = p->seq_number % MAX_BACKLOG;
	p->backlog[n].seq_number = p->seq_number;
	p->backlog[n].timestamp = p->rtp_ts.audio;
	if (p->backlog[n].buffer) free(p->backlog[n].buffer);
	p->backlog[n].buffer = buffer;
	p->backlog[n].size = sizeof(rtp_audio_pkt_t) + size;
	pthread_mutex_unlock(&p->mutex);

	FD_ZERO(&wfds);
	FD_SET(p->rtp_ports.audio.fd, &wfds);

	//LOG_SDEBUG("[%p]: sending buffer %u (n:%u)", p, gettime_ms(), now);

	if (select(p->rtp_ports.audio.fd + 1, NULL, &wfds, NULL, &timeout) == -1) {
		LOG_ERROR("[%p]: audio socket closed", p);
		p->sane += 5;
	}

	if (FD_ISSET(p->rtp_ports.audio.fd, &wfds)) {
		n = sendto(p->rtp_ports.audio.fd, (void*) packet, sizeof(rtp_audio_pkt_t) + size, 0, (void*) &addr, sizeof(addr));
		if (n != sizeof(rtp_audio_pkt_t) + size) {
			LOG_ERROR("[%p]: error sending audio packet", p);
			p->sane += 2;
		}
		usleep((frames * 1000000LL) / (p->sample_rate * 2));
	}
	else {
		LOG_ERROR("[%p]: audio socket unavailable", p);
		p->sane++;
	}

	//LOG_SDEBUG("[%p]: sent buffer %u", p, gettime_ms());

	if (playtime % 10000 < 8) {
		LOG_INFO("check n:%u p:%u tsa:%u sn:%u", now, playtime, p->rtp_ts.audio, p->seq_number);
	}

	p->playtime = playtime - read_ahead;

	return playtime;
}


/*----------------------------------------------------------------------------*/
struct raopcl_s *raopcl_create(char *local, raop_codec_t codec, raop_crypto_t crypto, int sample_rate, int sample_size, int channels, int volume)
{
	raopcl_data_t *raopcld;

	// seed random generator
	raopcld = malloc(sizeof(raopcl_data_t));
	RAND_seed(raopcld, sizeof(raopcl_data_t));
	memset(raopcld, 0, sizeof(raopcl_data_t));

	raopcld->state = RAOP_DOWN_FULL;
	raopcld->sane = 0;
	raopcld->sample_rate = sample_rate;
	raopcld->sample_size = sample_size;
	raopcld->channels = channels;
	raopcld->volume = volume;
	raopcld->codec = codec;
	raopcld->crypto = crypto;
	if (!local || strstr(local, "?")) raopcld->local_addr.s_addr = INADDR_ANY;
	else raopcld->local_addr.s_addr = inet_addr(local);
	raopcld->rtp_ports.ctrl.fd = raopcld->rtp_ports.time.fd = raopcld->rtp_ports.audio.fd = -1;

	// need to init that as well as squeezelite will immediately produce audio
	raopcld->rtp_ts.first_clock = gettime_ms();
	raopcld->rtp_ts.audio = raopcld->rtp_ts.first = raopcld->rtp_ts.first_clock;

	// init RTSP if needed
	if (((raopcld->rtspcl = rtspcl_create("iTunes/10.6.2 (Windows; N;)")) == NULL)) {
		LOG_ERROR("[%p]: Cannot create RTSP contex", raopcld);
		free(raopcld);
		return NULL;
	}

	pthread_mutex_init(&raopcld->mutex, NULL);

	RAND_bytes(raopcld->iv, sizeof(raopcld->iv));
	VALGRIND_MAKE_MEM_DEFINED(raopcld->iv, sizeof(raopcld->iv));
	RAND_bytes(raopcld->key, sizeof(raopcld->key));
	VALGRIND_MAKE_MEM_DEFINED(raopcld->key, sizeof(raopcld->key));

	memcpy(raopcld->nv, raopcld->iv, sizeof(raopcld->nv));
	aes_set_key(&raopcld->ctx, raopcld->key, 128);

	return raopcld;
}


/*----------------------------------------------------------------------------*/
void raopcl_terminate_rtp(struct raopcl_s *p)
{
	int i;

	// Terminate RTP threads and close sockets
	p->ctrl_running = false;
	pthread_join(p->ctrl_thread, NULL);

	p->time_running = false;
	pthread_join(p->time_thread, NULL);

	if (p->rtp_ports.ctrl.fd != -1) close(p->rtp_ports.ctrl.fd);
	if (p->rtp_ports.time.fd != -1) close(p->rtp_ports.time.fd);
	if (p->rtp_ports.audio.fd != -1) close(p->rtp_ports.audio.fd);

	p->rtp_ports.ctrl.fd = p->rtp_ports.time.fd = p->rtp_ports.audio.fd = -1;

	for (i = 0; i < MAX_BACKLOG; i++) {
		pthread_mutex_lock(&p->mutex);
		if (p->backlog[i].buffer) {
			free(p->backlog[i].buffer);
			p->backlog[i].buffer = NULL;
		}
		pthread_mutex_unlock(&p->mutex);
	}
}


/*----------------------------------------------------------------------------*/
// minimum=0, maximum=100
bool raopcl_update_volume(struct raopcl_s *p, int vol, bool force)
{
	char a[128];

	if (!p || !p->rtspcl || p->state < RAOP_FLUSHED) return false;

	if (!force && vol == p->volume) return true;

	p->volume = vol;

	if (vol == 0) vol = -144.0;
	else vol = VOLUME_MIN + ((VOLUME_MAX - VOLUME_MIN) * vol) / 100;

	sprintf(a, "volume: %d.0\r\n", vol);

	return rtspcl_set_parameter(p->rtspcl, a);
}


/*----------------------------------------------------------------------------*/
bool raopcl_progress(struct raopcl_s *p, __u32 start, __u32 duration)
{
	char a[128];
	__u32 end;

	if (!p || !p->rtspcl || p->state < RAOP_FLUSHED) return false;

	if (duration)
		end = (__u32) (((__u64) (start + duration - p->rtp_ts.first_clock) * p->sample_rate) / 1000 + p->rtp_ts.first);
	else
		end = (__u32) p->rtp_ts.audio + (3599L * p->sample_rate);

	start = (__u32) (((__u64) (start - p->rtp_ts.first_clock) * p->sample_rate) / 1000 + p->rtp_ts.first);

	/*
	This is very hacky: it works only because the RTP counter is derived from
	the main clock in ms
	*/
	sprintf(a, "progress: %u/%u/%u\r\n", start, p->rtp_ts.audio, end);

	return rtspcl_set_parameter(p->rtspcl, a);
}


/*----------------------------------------------------------------------------*/
bool raopcl_set_daap(struct raopcl_s *p, int count, ...)
{
	va_list args;

	if (!p) return false;

	va_start(args, count);

	return rtspcl_set_daap(p->rtspcl, p->rtp_ts.audio, count, args);
}



/*----------------------------------------------------------------------------*/
static bool raopcl_set_sdp(struct raopcl_s *p, char *sdp)
{
	bool rc = true;

   // codec
	switch (p->codec) {

		case RAOP_ALAC: {
			char buf[256];

			sprintf(buf,
					"m=audio 0 RTP/AVP 96\r\n"
					"a=rtpmap:96 AppleLossless\r\n"
					"a=fmtp:96 %d 0 %d 40 10 14 %d 255 0 0 %d\r\n",
					MAX_SAMPLES_IN_CHUNK, p->sample_size, p->channels, p->sample_rate);
			/* maybe one day I'll figure out how to send raw PCM ...
			sprintf(buf,
					"m=audio 0 RTP/AVP 96\r\n"
					"a=rtpmap:96 L16/44100/2\r\n",
			*/
			strcat(sdp, buf);
			break;
		}
		default:
			rc = false;
			LOG_ERROR("[%p]: unsupported codec: %d", p, p->codec);
			break;
	}

	// add encryption if required - only RSA
	switch (p->crypto ) {
		case RAOP_RSA: {
			char *key = NULL, *iv = NULL, *buf;
			__u8 rsakey[512];
			int i;

			i = rsa_encrypt(p->key, 16, rsakey);
			base64_encode(rsakey, i, &key);
			remove_char_from_string(key, '=');
			base64_encode(p->iv, 16, &iv);
			remove_char_from_string(iv, '=');
			buf = malloc(strlen(key) + strlen(iv) + 128);
			sprintf(buf, "a=rsaaeskey:%s\r\n"
						"a=aesiv:%s\r\n",
						key, iv);
			strcat(sdp, buf);
			free(key);
			free(iv);
            free(buf);
		}
		case RAOP_CLEAR:
			break;
		default:
			rc = false;
			LOG_ERROR("[%p]: unsupported encryption: %d", p, p->crypto);
	}

	return rc;
}


/*----------------------------------------------------------------------------*/
static bool raopcl_analyse_setup(struct raopcl_s *p, key_data_t *setup_kd)
{
	char *buf, *token, *pc;
	const char delimiters[] = ";";
	bool rc = true;

#if 0
	// get audio jack info
	if ((buf = kd_lookup(setup_kd,"Audio-Jack-Status")) == NULL) {
		LOG_ERROR("[%p]: Audio-Jack-Status is missing", p);
		rc = false;
	}

	token = strtok(buf,delimiters);
	while(token){
		if ((pc = strstr(token, "=")) != NULL){
			*pc = 0;
			if(!strcmp(token,"type") && !strcmp(pc + 1,"digital")) p->ajtype = JACK_TYPE_DIGITAL;
		}
		else {
			if (!strcmp(token,"connected")) p->ajstatus = JACK_STATUS_CONNECTED;
		}
		token = strtok(NULL, delimiters);
	}
#endif

	// get transport (port ...) info
	if ((buf = kd_lookup(setup_kd, "Transport")) == NULL){
		LOG_ERROR("[%p]: no transport in response", p);
		rc = false;
	}

	token = strtok(buf, delimiters);
	while (token) {
		if ((pc = strstr(token, "=")) != NULL) {
			*pc = 0;
			if (!strcmp(token,"server_port")) p->rtp_ports.audio.rport=atoi(pc+1);
			if (!strcmp(token,"control_port")) p->rtp_ports.ctrl.rport=atoi(pc+1);
			if (!strcmp(token,"timing_port")) p->rtp_ports.time.rport=atoi(pc+1);
		}
		token = strtok(NULL,delimiters);
	}

	if (!p->rtp_ports.audio.rport || !p->rtp_ports.ctrl.rport || !p->rtp_ports.time.rport) {
		LOG_ERROR("[%p]: missing a RTP port in response", p);
		rc = false;
	}

	return rc;
}


/*----------------------------------------------------------------------------*/
bool raopcl_reconnect(struct raopcl_s *p) {
   return raopcl_connect(p, p->host_addr, p->rtsp_port, p->codec);
}


/*----------------------------------------------------------------------------*/
bool raopcl_connect(struct raopcl_s *p, struct in_addr host, __u16 destport, raop_codec_t codec)
{
	struct {
		__u32 sid;
		__u64 sci;
		__u8 sac[16];
	} seed;
	char sid[10+1], sci[16+1];
	char *sac = NULL;
	char sdp[1024];
	bool rc = false;
	key_data_t kd[MAX_KD];
	char *buf;

	if (!p) return false;

	if (p->state >= RAOP_FLUSHING) return true;

	if (host.s_addr != INADDR_ANY) p->host_addr.s_addr = host.s_addr;
	if (codec != RAOP_NOCODEC) p->codec = codec;
	if (destport != 0) p->rtsp_port = destport;

	p->ssrc = _random(0xffffffff);
	p->latency = 0;
	p->encrypt = (p->crypto != RAOP_CLEAR);
	p->sane = 0;

	RAND_bytes((__u8*) &seed, sizeof(seed));
	VALGRIND_MAKE_MEM_DEFINED(&seed, sizeof(seed));
	sprintf(sid, "%10lu", (long unsigned int) seed.sid);
	sprintf(sci, "%016llx", (long long int) seed.sci);

	// RTSP misc setup
	if (!rtspcl_add_exthds(p->rtspcl,"Client-Instance", sci)) goto erexit;
	//if (rtspcl_add_exthds(p->rtspcl,"Active-Remote", "1986535575")) goto erexit;
	//if (rtspcl_add_exthds(p->rtspcl,"DACP-ID", sci)) goto erexit;

	// RTSP connect
	if (!rtspcl_connect(p->rtspcl, p->local_addr, host, destport, sid)) goto erexit;

	LOG_INFO("[%p]: local interface %s", p, rtspcl_local_ip(p->rtspcl));

	// RTSP auth
	//if(rtspcl_auth_setup(p->rtspcl)) goto erexit;

	// RTSP get options
	if (p->state == RAOP_DOWN_FULL && !rtspcl_options(p->rtspcl)) goto erexit;

	// build sdp parameter
	buf = strdup(inet_ntoa(host));
	sprintf(sdp,
			"v=0\r\n"
			"o=iTunes %s 0 IN IP4 %s\r\n"
			"s=iTunes\r\n"
			"c=IN IP4 %s\r\n"
			"t=0 0\r\n",
			sid, rtspcl_local_ip(p->rtspcl), buf);
	free(buf);

	if (!raopcl_set_sdp(p, sdp)) goto erexit;

	// RTSP ANNOUNCE
	base64_encode(&seed.sac, 16, &sac);
	remove_char_from_string(sac, '=');
	if (!rtspcl_add_exthds(p->rtspcl, "Apple-Challenge", sac)) goto erexit;
	if (!rtspcl_announce_sdp(p->rtspcl, sdp)) goto erexit;
	if (!rtspcl_mark_del_exthds(p->rtspcl, "Apple-Challenge")) goto erexit;

	// open RTP sockets, need local ports here before sending SETUP
	p->rtp_ports.ctrl.lport = p->rtp_ports.time.lport = p->rtp_ports.audio.lport = 0;
	if ((p->rtp_ports.ctrl.fd = open_udp_socket(p->local_addr, &p->rtp_ports.ctrl.lport, true)) == -1) goto erexit;
	if ((p->rtp_ports.time.fd = open_udp_socket(p->local_addr, &p->rtp_ports.time.lport, true)) == -1) goto erexit;
	if ((p->rtp_ports.audio.fd = open_udp_socket(p->local_addr, &p->rtp_ports.audio.lport, true)) == -1) goto erexit;

	// RTSP SETUP : get all RTP destination ports
	if (!rtspcl_setup(p->rtspcl, &p->rtp_ports, kd)) goto erexit;
	if (!raopcl_analyse_setup(p, kd)) goto erexit;
	free_kd(kd);

	LOG_DEBUG( "[%p]: opened audio socket   l:%5d r:%d", p, p->rtp_ports.audio.lport, p->rtp_ports.audio.rport );
	LOG_DEBUG( "[%p]:opened timing socket  l:%5d r:%d", p, p->rtp_ports.time.lport, p->rtp_ports.time.rport );
	LOG_DEBUG( "[%p]:opened control socket l:%5d r:%d", p, p->rtp_ports.ctrl.lport, p->rtp_ports.ctrl.rport );

	p->seq_number = _random(0xffff);

	// now timing thread can start
	p->time_running = true;
	pthread_create(&p->time_thread, NULL, rtp_timing_thread, (void*) p);

	rc = rtspcl_record(p->rtspcl, p->seq_number, p->rtp_ts.first, kd);
	if (kd_lookup(kd, "Audio-Latency")) p->latency = atoi(kd_lookup(kd, "Audio-Latency"));
	free_kd(kd);

	p->ctrl_running = true;
	pthread_create(&p->ctrl_thread, NULL, rtp_control_thread, (void*) p);

	pthread_mutex_lock(&p->mutex);
	p->state = RAOP_FLUSHED;
	p->flush_mode = RAOP_RECLOCK;
	pthread_mutex_unlock(&p->mutex);

	// if the above fails for any reason, tear everything down
	if (!rc) raopcl_disconnect(p);

	if (!raopcl_update_volume(p, p->volume, true)) goto erexit;

 erexit:
	if (sac) free(sac);
	return rc;
}


/*----------------------------------------------------------------------------*/
bool raopcl_flush_stream(struct raopcl_s *p, raop_flush_t mode)
{
	bool rc;
	__u16 seq_number;
	__u32 timestamp;


	if (!p || p->state != RAOP_STREAMING) return false;

	pthread_mutex_lock(&p->mutex);
	p->state = RAOP_FLUSHING;
	seq_number = p->seq_number;
	timestamp = p->rtp_ts.audio;
	pthread_mutex_unlock(&p->mutex);

	LOG_INFO("[%p]: flushing up to s:%u ts:%u", p, seq_number, timestamp);

	// everything BELOW these values should be FLUSHED
	rc = rtspcl_flush(p->rtspcl, seq_number + 1, timestamp + 1);

	pthread_mutex_lock(&p->mutex);
	p->state = RAOP_FLUSHED;
	p->flush_mode = mode;
	pthread_mutex_unlock(&p->mutex);

	return rc;
}


/*----------------------------------------------------------------------------*/
bool raopcl_disconnect(struct raopcl_s *p)
{
	bool rc = true;

	if (!p) return false;

	if (p->state >= RAOP_FLUSHING) {
		rc = raopcl_teardown(p);
		rc &= rtspcl_disconnect(p->rtspcl);
	}

	pthread_mutex_lock(&p->mutex);
	p->state = RAOP_DOWN_FULL;
	pthread_mutex_unlock(&p->mutex);

	return rc;
}


/*----------------------------------------------------------------------------*/
bool raopcl_teardown(struct raopcl_s *p)
{
	if (!p || p->state < RAOP_FLUSHING) return false;

	raopcl_terminate_rtp(p);

	pthread_mutex_lock(&p->mutex);
	p->state = RAOP_DOWN;
	pthread_mutex_unlock(&p->mutex);

	rtspcl_remove_all_exthds(p->rtspcl);

	return rtspcl_teardown(p->rtspcl);
}



/*----------------------------------------------------------------------------*/
bool raopcl_destroy(struct raopcl_s *p)
{
	bool rc;

	if (!p) return false;

	raopcl_terminate_rtp(p);
	rc = rtspcl_destroy(p->rtspcl);
	pthread_mutex_destroy(&p->mutex);
	free(p);

	return rc;
}


/*----------------------------------------------------------------------------*/
void raopcl_send_sync(struct raopcl_s *raopcld, bool first)
{
	struct sockaddr_in addr;
	ntp_t curr_time;
	rtp_sync_pkt_t rsp;
	__u32 now;
	int n;

	addr.sin_family = AF_INET;
	addr.sin_addr = raopcld->host_addr;
	addr.sin_port = htons(raopcld->rtp_ports.ctrl.rport);

	// do not send timesync on FLUSHED
	if (raopcld->state != RAOP_STREAMING) return;

	rsp.hdr.proto = 0x80 | (first ? 0x10 : 0x00);
	rsp.hdr.type = 0x54 | 0x80;
	// seems that seq=7 shall be forced
	rsp.hdr.seq[0] = 0;
	rsp.hdr.seq[1] = 7;

	get_ntp(&curr_time);
	now = gettime_ms();

	/*
	Seems that timestamp unit must be number of samples, not in ms. This
	below is not good accuracy, but no error is cumulated and rollover is
	contained
	*/
	rsp.rtp_timestamp = now - raopcld->rtp_ts.first_clock;
	rsp.rtp_timestamp = ((__u64) rsp.rtp_timestamp * raopcld->sample_rate) / 1000L;
	rsp.rtp_timestamp += raopcld->rtp_ts.first;

	raopcld->rtp_ts.latest = rsp.rtp_timestamp;

	LOG_DEBUG( "[%p]: sync ntp:%u.%u (ts:%lu) (now:%lu-ms) (el:%lu-ms)", raopcld, curr_time.seconds,
			  curr_time.fraction, rsp.rtp_timestamp, now, now - raopcld->rtp_ts.first_clock);

	// set the NTP time and go to network order
	rsp.curr_time.seconds = htonl(curr_time.seconds);
	rsp.curr_time.fraction = htonl(curr_time.fraction);

	// network order
	rsp.rtp_timestamp_latency = htonl(rsp.rtp_timestamp - raopcld->latency);
	rsp.rtp_timestamp = htonl(rsp.rtp_timestamp);

	n = sendto(raopcld->rtp_ports.ctrl.fd, (void*) &rsp, sizeof(rsp), 0, (void*) &addr, sizeof(addr));

	if (n < 0) LOG_ERROR("[%p]: write error: %s", raopcld, strerror(errno));
	if (n == 0) LOG_INFO("[%p]: write, disconnected on the other end", raopcld);
}


/*----------------------------------------------------------------------------*/
void *rtp_timing_thread(void *args)
{
	raopcl_data_t *raopcld = (raopcl_data_t*) args;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_addr = raopcld->host_addr;
	addr.sin_port = htons(raopcld->rtp_ports.time.rport);

	while (raopcld->time_running)
	{
		rtp_time_pkt_t req;
		struct timeval timeout = { 1, 0 };
		fd_set rfds;
		int n;

		FD_ZERO(&rfds);
		FD_SET(raopcld->rtp_ports.time.fd, &rfds);

		if ((n = select(raopcld->rtp_ports.time.fd + 1, &rfds, NULL, NULL, &timeout)) == -1) {
			LOG_ERROR("[%p]: raopcl_time_connect: socket closed on the other end", raopcld);
			sleep(1);
			break;
		}

		if (!FD_ISSET(raopcld->rtp_ports.time.fd, &rfds)) continue;

		n = recv(raopcld->rtp_ports.time.fd, (void*) &req, sizeof(req), 0);

		if( n > 0) 	{
			rtp_time_pkt_t rsp;

			rsp.hdr = req.hdr;
			rsp.hdr.type = 0x53 | 0x80;
			// just copy the request header or set seq=7 and timestamp=0
			rsp.ref_time = req.send_time;
			VALGRIND_MAKE_MEM_DEFINED(&rsp, sizeof(rsp));

			// transform timeval into NTP and set network order
			get_ntp(&rsp.send_time);
			LOG_DEBUG( "[%p]: NTP sync: %u.%u (ref %u.%u)", raopcld, rsp.send_time.seconds, rsp.send_time.fraction,
													 ntohl(rsp.ref_time.seconds), ntohl(rsp.ref_time.fraction) );

			rsp.send_time.seconds = htonl(rsp.send_time.seconds);
			rsp.send_time.fraction = htonl(rsp.send_time.fraction);
			rsp.recv_time = rsp.send_time; // might need to add a few fraction ?

			n = sendto(raopcld->rtp_ports.time.fd, (void*) &rsp, sizeof(rsp), 0, (void*) &addr, sizeof(addr));

			if (n != (int) sizeof(rsp)) {
			   LOG_ERROR("[%p]: error responding to sync", raopcld);
			}
		}

		if (n < 0) {
		   LOG_ERROR("[%p]: read error: %s", raopcld, strerror(errno));
		}

		if (n == 0) {
			LOG_ERROR("[%p]: read, disconnected on the other end", raopcld);
			sleep(1);
			break;
		}
	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
void *rtp_control_thread(void *args)
{
	raopcl_data_t *raopcld = (raopcl_data_t*) args;

	while (raopcld->ctrl_running)	{
		struct timeval timeout = { 1, 0 };
		fd_set rfds;

		FD_ZERO(&rfds);
		FD_SET(raopcld->rtp_ports.ctrl.fd, &rfds);

		if (select(raopcld->rtp_ports.ctrl.fd + 1, &rfds, NULL, NULL, &timeout) == -1) {
			if (raopcld->ctrl_running) {
				LOG_ERROR("[%p]: control socket closed", raopcld);
				sleep(1);
			}
			continue;
		}

		if (FD_ISSET(raopcld->rtp_ports.ctrl.fd, &rfds)) {
			rtp_lost_pkt_t lost;
			bool error = false;
			int i, n, missed;

			n = recv(raopcld->rtp_ports.ctrl.fd, (void*) &lost, sizeof(lost), 0);

			if (n < 0) continue;

			lost.seq_number = ntohs(lost.seq_number);
			lost.n = ntohs(lost.n);

			if (n != sizeof(lost)) {
				LOG_ERROR("[%p]: error in received request sn:%d n:%d (recv:%d)",
						  raopcld, lost.seq_number, lost.n, n);
				lost.n = 0;
				lost.seq_number = 0;
				error = true;
			}

			for (missed = 0, i = 0; i < lost.n; i++) {
				u16_t index = (lost.seq_number + i) % MAX_BACKLOG;

				if (raopcld->backlog[index].seq_number == lost.seq_number + i) {
					struct sockaddr_in addr;
					rtp_header_t *hdr = (rtp_header_t*) raopcld->backlog[index].buffer;

					hdr->proto = 0x80;
					hdr->type = 0x56 | 0x80;
					hdr->seq[0] = 0;
					hdr->seq[1] = 1;

					addr.sin_family = AF_INET;
					addr.sin_addr = raopcld->host_addr;
					addr.sin_port = htons(raopcld->rtp_ports.ctrl.rport);

					n = sendto(raopcld->rtp_ports.ctrl.fd, (void*) hdr,
							   sizeof(rtp_header_t) + raopcld->backlog[index].size,
							   0, (void*) &addr, sizeof(addr));

					if (n == -1) {
						error = true;
						LOG_DEBUG("[%p]: error resending lost packet sn:%u (n:%d)",
								   raopcld, lost.seq_number + i, n);
					}
				}
				else {
					missed++;
					LOG_DEBUG("[%p]: lost packet out of backlog %u", raopcld, lost.seq_number + i);
				}
			}

			LOG_ERROR("[%p]: retransmit packet sn:%d nb:%d (mis:%d) (err:%d)",
					  raopcld, lost.seq_number, lost.n, missed, error);

			if (error || missed > 100) {
				LOG_ERROR("[%p]: ctrl socket error", raopcld);
				raopcld->sane += 5;
			}

			continue;
		}

		raopcl_send_sync(raopcld, false);
	}

	return NULL;
}




