/*
 *  Squeeze2raop - LMS to RAOP gateway
 *
 *  Squeezelite : (c) Adrian Smith 2012-2014, triode1@btinternet.com
 *  Additions & gateway : (c) Philippe 2016, philippe_44@outlook.com
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

#ifndef __RAOPCORE_H
#define __RAOPCORE_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "squeezedefs.h"
#include "squeeze2raop.h"
#include "util_common.h"
#include "raop_client.h"

typedef struct sRaopCtx {
	bool			running;
	void			*owner;
	struct raopcl_s	*raopcl;
	struct in_addr	host;
	u16_t			port;
	pthread_t 		Thread;
	tQueue			reqQueue;
	pthread_mutex_t	reqMutex;
	pthread_cond_t	reqCond;
	u32_t 			LastFlush;
	bool			TearDownWait;
	u32_t			TearDownTO;
} tRaopCtx;

typedef struct sRaopReq {
	char Type[20];
	union {
		u8_t Volume;
		raop_codec_t Codec;
		raop_flush_t FlushMode;
	} Data;
} tRaopReq;


#endif
