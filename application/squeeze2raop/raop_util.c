/*
 *  Squeeze2raop - LMS to RAOP gateway
 *
 *  Squeezelite : (c) Adrian Smith 2012-2014, triode1@btinternet.com
 *  Additions & gateway : (c) Philippe 2014, philippe_44@outlook.com
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


#include <stdlib.h>
#include <math.h>

#include "squeezedefs.h"
#include "util_common.h"
#include "util.h"
#include "raop_client.h"
#include "raop_util.h"
#include "squeeze2raop.h"
#include "raop_core.h"

extern log_level	raop_loglevel;
static log_level 	*loglevel = &raop_loglevel;

void RaopFlush(struct sRaopCtx *Ctx, raop_flush_t Mode);


/*----------------------------------------------------------------------------*/
bool RaopConnect(struct sRaopCtx *Ctx, raop_codec_t Codec, struct sq_metadata_s *MetaData)
{
	tRaopReq *Req = malloc(sizeof(tRaopReq));

	Req->Data.Codec = Codec;
	strcpy(Req->Type, "CONNECT");
	pthread_mutex_lock(&Ctx->reqMutex);
	QueueInsert(&Ctx->reqQueue, Req);
	pthread_cond_signal(&Ctx->reqCond);
	pthread_mutex_unlock(&Ctx->reqMutex);

	return true;
}


/*----------------------------------------------------------------------------*/
void RaopStop(struct sRaopCtx *Ctx)
{
	LOG_INFO("[%p]: stop request", Ctx->owner);
	RaopFlush(Ctx, RAOP_RECLOCK);
}


/*----------------------------------------------------------------------------*/
void RaopPause(struct sRaopCtx *Ctx)
{
	LOG_INFO("[%p]: pause request", Ctx->owner);
	RaopFlush(Ctx, RAOP_REBUFFER);
}


/*----------------------------------------------------------------------------*/
void RaopUnPause(struct sRaopCtx *Ctx)
{
	LOG_INFO("[%p]: un-pause request", Ctx->owner);
	RaopConnect(Ctx, RAOP_NOPARAM, NULL);
}


/*----------------------------------------------------------------------------*/
void RaopFlush(struct sRaopCtx *Ctx, raop_flush_t mode)
{
	tRaopReq *Req = malloc(sizeof(tRaopReq));

	strcpy(Req->Type, "FLUSH");
	Req->Data.FlushMode = mode;
	pthread_mutex_lock(&Ctx->reqMutex);
	QueueInsert(&Ctx->reqQueue, Req);
	pthread_cond_signal(&Ctx->reqCond);
	pthread_mutex_unlock(&Ctx->reqMutex);
}


/*----------------------------------------------------------------------------*/
void RaopDisconnect(struct sRaopCtx *Ctx)
{
	tRaopReq *Req = malloc(sizeof(tRaopReq));

	LOG_INFO("[%p]: full disconnect request", Ctx->owner);
	pthread_mutex_lock(&Ctx->reqMutex);
	QueueFlush(&Ctx->reqQueue);
	strcpy(Req->Type, "OFF");
	QueueInsert(&Ctx->reqQueue, Req);
	pthread_cond_signal(&Ctx->reqCond);
	pthread_mutex_unlock(&Ctx->reqMutex);
}


/*----------------------------------------------------------------------------*/
void RaopSetVolume(struct sRaopCtx *Ctx, u8_t Volume)
{
	tRaopReq *Req = malloc(sizeof(tRaopReq));
	//return;

	if (Volume > 100) Volume = 100;

	LOG_INFO("[%p]: volume request", Ctx->owner);
	Req->Data.Volume = Volume;
	strcpy(Req->Type, "VOLUME");
	pthread_mutex_lock(&Ctx->reqMutex);
	QueueInsert(&Ctx->reqQueue, Req);
	pthread_cond_signal(&Ctx->reqCond);
	pthread_mutex_unlock(&Ctx->reqMutex);
}


/*----------------------------------------------------------------------------*/
void RaopSetDeviceVolume(struct sRaopCtx *Ctx, u8_t Volume)
{
	if (Volume > 100) Volume = 100;
}







