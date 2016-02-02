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
log_level	raop_loglevel = lDEBUG;

log_level main_log = lINFO;
log_level *loglevel =&main_log;

static int print_usage(char *argv[])
{
	printf("%s [-p port_number] [-v volume(0-100)] "
			   "[-s startms] [-u endms] [-b [0-100]] [-d [-30-+30(s)]] "
		   "[-i interactive mode] [-e no-encrypt-mode] server_ip audio_filename\n",argv[0]);
	return -1;
}

u32_t gettime_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


u64_t timeval_to_ntp(struct timeval tv, struct ntp_s *ntp)
{
	struct ntp_s local;

	local.seconds  = tv.tv_sec + 0x83AA7E80;
	local.fraction = (((__u64) tv.tv_usec) << 32) / 1000000;

	if (ntp) *ntp = local;

	return (((__u64) local.seconds) << 32) + local.fraction;
}


u64_t get_ntp(struct ntp_s *ntp)
{
	struct timeval ctv;

	gettimeofday(&ctv, NULL);
	return timeval_to_ntp(ctv, ntp);
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
	int n;
	raop_crypto_t crypto;

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

	//freopen("log.txt", "w", stderr);
	if ((raopcl = raopcl_create("?", RAOP_ALAC, RAOP_CLEAR, 44100, 16, 2, 50)) == NULL) {
		LOG_ERROR("Cannot init RAOP", 0);
		exit(1);
	}

	host_ip = gethostbyname(host);
	memcpy(&host_addr.s_addr, host_ip->h_addr_list[0], host_ip->h_length);

	if (!raopcl_connect(raopcl, host_addr, port, RAOP_ALAC)) {
		free(raopcl);
		LOG_ERROR("Cannot connect to AirPlay device %s", host);
		exit(1);
	}

	infile = fopen("test.pcm", "rb");
	buf = malloc(2000);
	LOG_INFO( "%s to %s\n", RAOP_CONNECTED, host );
	//raopcl_update_volume(raopcl, volume, true);

	do {
		u8_t *buffer, *buf2;
		u32_t timestamp;
		int i;

		n = fread(buf, 352*4, 1, infile);
		pcm_to_alac(buf, 352, &buffer, &size, 352, 2, false);
		pcm_to_alac_fast(buf, 352, &buf2, &size, 352);
		for (i = 0; i < size; i++) {
			if (buf2[i] != buffer[i]) {
				printf("error");
			}
		}
		// buffer = buf;
		timestamp = raopcl_send_sample(raopcl, buffer, size, 352, false, 3000);
		free(buffer);
		free(buf2);
	} while (n);

 erexit:

	raopcl_destroy(raopcl);

	free(buf);

#if WIN
	winsock_close();
#endif
	return rval;
}



