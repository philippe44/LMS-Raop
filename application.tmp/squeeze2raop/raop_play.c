/*****************************************************************************
 * rtsp_play.c: RAOP Client player
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

#include <stdio.h>
#include <signal.h>
#include "platform.h"
#include "aexcl_lib.h"
#include "raop_client.h"
#include "alac_wrapper.h"
#include "raop_play.h"

log_level	util_loglevel = lINFO;
log_level	raop_loglevel = lINFO;

log_level main_log = lINFO;
log_level *loglevel =&main_log;

static int print_usage(char *argv[])
{
	printf("%s [-p port_number] [-v volume(0-100)]"
			   "[-e no-encrypt-mode] server_ip audio_filename\n",argv[0]);
	return -1;
}

u32_t gettime_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (u32_t) ((u64_t) (tv.tv_sec + 0x83AA7E80) * 1000 + (u64_t) tv.tv_usec / 1000);
}

u64_t get_ntp(struct ntp_s *ntp)
{
	struct timeval ctv;
	struct ntp_s local;

	gettimeofday(&ctv, NULL);
	local.seconds  = ctv.tv_sec + 0x83AA7E80;
	local.fraction = (((__u64) ctv.tv_usec) << 32) / 1000000;

	if (ntp) *ntp = local;

	return (((__u64) local.seconds) << 32) + local.fraction;
}


#if WIN
void winsock_init(void) {
	WSADATA wsaData;
	WORD wVersionRequested = MAKEWORD(2, 2);
	int WSerr = WSAStartup(wVersionRequested, &wsaData);
	if (WSerr != 0) exit(1);
}

void winsock_close(void) {
	WSACleanup();
}
#endif

int main(int argc, char *argv[])
{
	struct raopcl_s *raopcl;
	char *host=NULL;
	char *fname=NULL;
	int port=SERVER_PORT;
	int rval=-1,i;
	int size;
	int volume = 50;
	struct hostent *host_ip;
	struct in_addr host_addr;
	FILE *infile;
	u8_t *buf;
	int n = -1;
	raop_crypto_t crypto;
	int count = 0;
	__u64 stamp;
	__u32 stamp2;
	bool pause = false;
	__u64 last = 0;
	__u32 frames, pause_frames = 0;

	for(i=1;i<argc;i++){
		if(!strcmp(argv[i],"-p")){
			port=atoi(argv[++i]);
			continue;
		}
		if(!strcmp(argv[i],"-v")){
			volume=atoi(argv[++i]);
			continue;
		}
		if(!strcmp(argv[i],"-e")){
			crypto = RAOP_CLEAR;
			continue;
		}
		if(!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h"))
			return print_usage(argv);
		if(!host) {host=argv[i]; continue;}
		if(!fname) {fname=argv[i]; continue;}
	}
	if (!host) return print_usage(argv);
	if (!fname) return print_usage(argv);

#if WIN
	winsock_init();
#endif

	if ((raopcl = raopcl_create("?", NULL, NULL, RAOP_ALAC, 352, 2000,
								RAOP_CLEAR, 44100, 16, 2, 50)) == NULL) {
		LOG_ERROR("Cannot init RAOP", 0);
		exit(1);
	}

	host_ip = gethostbyname(host);
	memcpy(&host_addr.s_addr, host_ip->h_addr_list[0], host_ip->h_length);

	infile = fopen("compteur.pcm", "rb");
	buf = malloc(2000);

	if (!raopcl_connect(raopcl, host_addr, port, RAOP_ALAC)) {
		free(raopcl);
		LOG_ERROR("Cannot connect to AirPlay device %s", host);
		exit(1);
	}

	getchar();
	stamp = get_ntp(NULL);
	// raopcl_set_start(raopcl, stamp + TS2NTP(11025, 44100));
	raopcl_set_start(raopcl, stamp + MS2NTP(200));

	LOG_INFO( "%s to %s\n", RAOP_CONNECTED, host );

	do {
		u8_t *buffer;
		u64_t timestamp;

		if (raopcl_avail_frames(raopcl) >= 352) {
			u64_t now = get_ntp(NULL);
			if (now - last > MS2NTP(500) && now > stamp) {
				u64_t elapsed = now - stamp;
				u32_t elapsed_frames = pause_frames + raopcl_elapsed_frames(raopcl);
				last = now;
				LOG_INFO("elapsed from NTP:%Lu from frames:%u (queued: %u)",
						  (elapsed * 1000) >> 32, (elapsed_frames * 1000) / 44100,
						  raopcl_queued_frames(raopcl));
			}
			if (kbhit()) {
				pause = !pause;

				if (pause) {
					raopcl_pause_mark(raopcl);
					raopcl_flush(raopcl);
					LOG_INFO("Pause: %u", (pause_frames * 1000) / 44100);
				}
				else {
					pause_frames += raopcl_elapsed_frames(raopcl);
				}
				getchar();
			}
			/*
			if (count++ < 1) {
				int i;
				for (i = 0; i < 352*4; i++) buf[i] = random(65536);
			}
			else */
			if (pause) continue;
			n = fread(buf, 352*4, 1, infile);
			pcm_to_alac_fast(buf, 352, &buffer, &size, 352);
			raopcl_send_chunk(raopcl, buffer, size, &timestamp);
			frames += 352;
			free(buffer);
		}
	} while (n);

	while (raopcl_elapsed_frames(raopcl) < frames);

	raopcl_destroy(raopcl);

	free(buf);

#if WIN
	winsock_close();
#endif
	return rval;
}



