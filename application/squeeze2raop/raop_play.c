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

#define SEC(ntp) ((__u32) ((ntp) >> 32))
#define FRAC(ntp) ((__u32) (ntp))
#define SECNTP(ntp) SEC(ntp),FRAC(ntp)

log_level	util_loglevel = lINFO;
log_level	raop_loglevel = lINFO;

log_level main_log = lINFO;
log_level *loglevel =&main_log;

static int print_usage(char *argv[])
{
	printf("%s [-p port_number] [-v volume(0-100)] "
			   "[-l AirPlay latency (frames)] [-q player queue (frames)] "
			   "[-e encrypt-RSA] [-w wait-to-start] "
			   "server_ip audio_filename\n",argv[0]);
	return -1;
}

#if !WIN
int kbhit()
{
	struct timeval tv;
	fd_set fds;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
	select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &fds);
}
#endif

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

int main(int argc, char *argv[]) {
	struct raopcl_s *raopcl;
	char *host, *fname;
	int port, size;
	int volume = 50, wait = 0, latency = 0, queue = MS2TS(1000, 44100);
	struct hostent *host_ip;
	struct in_addr host_addr;
	FILE *infile;
	u8_t *buf;
	int i, n = -1;
	enum {STOPPED, PAUSED, PLAYING } status;
	raop_crypto_t crypto = RAOP_CLEAR;
	__u64 last = 0, frames = 0;

	for(i = 1; i < argc; i++){
		if(!strcmp(argv[i],"-p")){
			port=atoi(argv[++i]);
			continue;
		}
		if(!strcmp(argv[i],"-v")){
			volume=atoi(argv[++i]);
			continue;
		}
		if(!strcmp(argv[i],"-w")){
			wait=atoi(argv[++i]);
			continue;
		}
		if(!strcmp(argv[i],"-l")){
			latency=atoi(argv[++i]);
			continue;
		}
		if(!strcmp(argv[i],"-q")){
			queue=atoi(argv[++i]);
			continue;
		}
		if(!strcmp(argv[i],"-e")){
			crypto = RAOP_RSA;
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

	if (!(infile = fopen(fname, "rb"))) {
		LOG_ERROR("cannot open file %s", fname);
		exit(0);
	}

	if ((raopcl = raopcl_create("?", NULL, NULL, RAOP_ALAC, MAX_SAMPLES_PER_CHUNK,
								queue, latency, crypto, 44100, 16, 2, volume)) == NULL) {
		LOG_ERROR("Cannot init RAOP %p", raopcl);
		exit(1);
	}

	host_ip = gethostbyname(host);
	memcpy(&host_addr.s_addr, host_ip->h_addr_list[0], host_ip->h_length);

	if (!raopcl_connect(raopcl, host_addr, port, RAOP_ALAC)) {
		raopcl_destroy(raopcl);
		free(raopcl);
		LOG_ERROR("Cannot connect to AirPlay device %s", host);
		exit(1);
	}

	latency = raopcl_latency(raopcl);

	LOG_INFO( "connected to %s with latency %d(ms)", host, NTP2MS(latency));

	if (wait) {
		__u64 start = get_ntp(NULL) + MS2NTP(wait) + TS2NTP(latency, raopcl_sample_rate(raopcl));

		LOG_INFO("start at NTP %u:%u", SECNTP(start));
		raopcl_start_at(raopcl, start);
	}

	buf = malloc(MAX_SAMPLES_PER_CHUNK*4);
	do {
		__u8 *buffer;
		__u64 playtime, now;

		now = get_ntp(NULL);
		if (now - last > NTP2MS(1000)) {
			__u64 played = TS2MS64(frames - raoplc_queued_frames(raopcl) -
						   latency, raoplc_sample_rate(raopcl));

			last = now;
			LOG_INFO("at %u.%u, played %u ms", SECNTP(now), played);
		}

		if (status == PLAYING && raopcl_avail_frames(raopcl) >= MAX_SAMPLES_PER_CHUNK) {
			n = fread(buf, MAX_SAMPLES_PER_CHUNK*4, 1, infile);
			pcm_to_alac_fast(buf, MAX_SAMPLES_PER_CHUNK, &buffer, &size, MAX_SAMPLES_PER_CHUNK);
			raopcl_send_chunk(raopcl, buffer, size, &playtime);
			frames += MAX_SAMPLES_PER_CHUNK;
			free(buffer);
		}

		if (kbhit()) {
			char c = getchar();

			switch (c) {
			case 'p':
				if (status == PLAYING) {
					raopcl_pause_mark(raopcl);
					raopcl_flush(raopcl);
					status = PAUSED;
					LOG_INFO("Pause at : %u.%u", SECNTP(get_ntp(NULL)));
				}
				else status = PAUSED;
				break;
			case 's':
				raopcl_stop(raopcl);
				raopcl_flush(raopcl);
				status = STOPPED;
				LOG_INFO("Stopped at : %u.%u", SECNTP(get_ntp(NULL)));
				break;
			case 'r':
				status = PLAYING;
				LOG_INFO("Re-started at : %u.%u", SECNTP(get_ntp(NULL)));
			default: break;
			}
		}

	} while (n);

	// finishing
	while (raopcl_queued_frames(raopcl));

	//raopcl_disconnect(raopcl};
	//raopcl_destroy(raopcl);
	free(buf);

#if WIN
	winsock_close();
#endif
	return 0;
}



