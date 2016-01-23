/*
 *  Squeeze2cast - LMS to RAOP gateway
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

#ifndef __RAOPITF_H
#define __RAOPITF_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "squeezedefs.h"
#include "squeeze2raop.h"
#include "raop_client.h"
#include "util_common.h"

struct sRaopCtx;

struct sRaopCtx *CreateRaopDevice(void *owner, char *local, struct in_addr host,
								 u16_t port, raop_codec_t codec, raop_crypto_t crypto,
								 u32_t TearDownTO, int sample_rate, int sample_size,
								 int channels, int volume);
void 			DestroyRaopDevice(struct sRaopCtx *Ctx);
struct raopcl_s *GetRaopcl(struct sRaopCtx *Ctx);
void 			UpdateRaopPort(struct sRaopCtx *Ctx, u16_t port);

#endif
