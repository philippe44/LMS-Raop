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
#include <stdarg.h>

#include "squeezedefs.h"
#include "squeeze2raop.h"
#include "util_common.h"
#include "util.h"
#include "raop_core.h"
#include "raop_itf.h"


extern log_level	raop_loglevel;
static log_level 	*loglevel = &raop_loglevel;

static void *RaopThread(void *args);


/*----------------------------------------------------------------------------*/
void UpdateRaopPort(struct sRaopCtx *Ctx, u16_t port)
{
	if (!Ctx) return;

	if (port != Ctx->port) {
		LOG_INFO("[%p]: updating port %d", Ctx, port);
		Ctx->port = port;
    }
}


/*----------------------------------------------------------------------------*/
struct raopcl_s *GetRaopcl(struct sRaopCtx *Ctx)
{
	return Ctx->raopcl;
}


/*----------------------------------------------------------------------------*/
struct sRaopCtx *CreateRaopDevice(void *owner, char *local, struct in_addr host,
								 u16_t port, raop_codec_t codec, raop_crypto_t crypto,
								 u32_t TearDownTO, int sample_rate, int sample_size,
								 int channels, int volume)
{
	struct raopcl_s *raopcld;
	struct sRaopCtx *Ctx;

	if ((raopcld = raopcl_create(local, codec, crypto, sample_rate, sample_size, channels, volume)) == NULL) return NULL;

	Ctx = malloc(sizeof(tRaopCtx));
	Ctx->host = host;
	Ctx->port = port;
	Ctx->owner = owner;
	Ctx->raopcl = raopcld;
	Ctx->TearDownWait = false;
	Ctx->TearDownTO = TearDownTO;

	QueueInit(&Ctx->reqQueue);

	pthread_mutex_init(&Ctx->reqMutex, 0);
	pthread_cond_init(&Ctx->reqCond, 0);
	pthread_create(&Ctx->Thread, NULL, &RaopThread, Ctx);

	return Ctx;
}


/*----------------------------------------------------------------------------*/
void DestroyRaopDevice(struct sRaopCtx *Ctx)
{

	raopcl_destroy(Ctx->raopcl);

	Ctx->running = false;
	pthread_mutex_lock(&Ctx->reqMutex);
	pthread_cond_signal(&Ctx->reqCond);
	pthread_mutex_unlock(&Ctx->reqMutex);
	pthread_join(Ctx->Thread, NULL);

	pthread_cond_destroy(&Ctx->reqCond);
	pthread_mutex_destroy(&Ctx->reqMutex);

	LOG_INFO("[%p]: Raop device stopped", Ctx->owner);
	free(Ctx);
}


/*----------------------------------------------------------------------------*/
tRaopReq *GetRequest(struct sRaopCtx *Ctx)
{
	tRaopReq *data;

	pthread_mutex_lock(&Ctx->reqMutex);
	data = QueueExtract(&Ctx->reqQueue);
	if (!data) pthread_cond_reltimedwait(&Ctx->reqCond, &Ctx->reqMutex, 1000);
	pthread_mutex_unlock(&Ctx->reqMutex);

	return data;
}


/*----------------------------------------------------------------------------*/
static void *RaopThread(void *args)
{
	tRaopCtx *Ctx = (tRaopCtx*) args;

	Ctx->running = true;

	while (Ctx->running) {
		tRaopReq *req = GetRequest(Ctx);

		// might be empty when exiting
		if (!req) {
			u32_t now = gettime_ms();

			LOG_DEBUG("[%p]: tick %u", Ctx->owner, now);
			if (Ctx->TearDownWait && now > Ctx->LastFlush + Ctx->TearDownTO) {
				LOG_INFO("[%p]: Tear down connection %u", Ctx->owner, now);
				raopcl_disconnect(Ctx->raopcl);
				Ctx->TearDownWait = false;
			}
			continue;
		}

		if (!strcasecmp(req->Type, "CONNECT")) {
			LOG_INFO("[%p]: raop connecting ...", Ctx->owner);
			if (raopcl_connect(Ctx->raopcl, Ctx->host, Ctx->port, req->Data.Codec)) {
				Ctx->TearDownWait = false;
				LOG_INFO("[%p]: raop connected", Ctx->owner);
			}
			else {
				LOG_ERROR("[%p]: raop failed to connected", Ctx->owner);
			}
		}

		if (!strcasecmp(req->Type, "FLUSH")) {
			LOG_INFO("[%p]: flushing ... (%d)", Ctx->owner, req->Data.FlushMode);
			Ctx->LastFlush = gettime_ms();
			Ctx->TearDownWait = true;
			raopcl_flush_stream(Ctx->raopcl, req->Data.FlushMode);
		}

		if (!strcasecmp(req->Type, "OFF")) {
			LOG_INFO("[%p]: processing off", Ctx->owner);
			raopcl_disconnect(Ctx->raopcl);
		}

		if (!strcasecmp(req->Type, "VOLUME")) {
			LOG_INFO("[%p]: processing volume", Ctx->owner);
			raopcl_update_volume(Ctx->raopcl, req->Data.Volume, false);
		}

		free(req);

	}

	return NULL;
}


