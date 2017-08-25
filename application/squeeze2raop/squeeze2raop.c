/*
 *  Squeeze2raop - LMS to RAOP gateway
 *
 *  (c) Philippe, philippe_44@outlook.com
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

#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#if WIN
#include <process.h>
#endif

#include "ixml.h"
#include "squeezedefs.h"
#include "squeeze2raop.h"
#include "conf_util.h"
#include "util_common.h"
#include "log_util.h"
#include "util.h"

#include "mdnssd-itf.h"
#include "http_fetcher.h"
#include "mdns.h"
#include "mdnsd.h"


/*----------------------------------------------------------------------------*/
/* globals initialized */
/*----------------------------------------------------------------------------*/
#if LINUX || FREEBSD
bool				glDaemonize = false;
#endif
char 				glInterface[16] = "?";
bool				glInteractive = true;
char				*glLogFile;
s32_t				glLogLimit = -1;
static char			*glPidFile = NULL;
static char			*glSaveConfigFile = NULL;
bool				glAutoSaveConfigFile = false;
bool				glGracefullShutdown = true;
int					gl_mDNSId;
char 				glDACPid[] = "1A2B3D4EA1B2C3D4";
char				glExcluded[SQ_STR_LENGTH] = "aircast;airupnp";

log_level	slimproto_loglevel = lINFO;
log_level	stream_loglevel = lWARN;
log_level	decode_loglevel = lWARN;
log_level	output_loglevel = lINFO;
log_level	main_loglevel = lINFO;
log_level	slimmain_loglevel = lINFO;
log_level	util_loglevel = lINFO;
log_level	raop_loglevel = lINFO;

tMRConfig			glMRConfig = {
							true,
							"",
							false,
							false,
							30,
							false,
							30,
							false,
							"",				// credentials
							1000,			// read_ahead
							2,
							-1,
							true,
							"-30:1, -15:50, 0:100",
							true,
							false,
							false,			// VolumeTrigger
							"stop",			// PreventPlayback
					};

static u8_t LMSVolumeMap[101] = {
				0, 1, 1, 1, 2, 2, 2, 3,  3,  4,
				5, 5, 6, 6, 7, 8, 9, 9, 10, 11,
				12, 13, 14, 15, 16, 16, 17, 18, 19, 20,
				22, 23, 24, 25, 26, 27, 28, 29, 30, 32,
				33, 34, 35, 37, 38, 39, 40, 42, 43, 44,
				46, 47, 48, 50, 51, 53, 54, 56, 57, 59,
				60, 61, 63, 65, 66, 68, 69, 71, 72, 74,
				75, 77, 79, 80, 82, 84, 85, 87, 89, 90,
				92, 94, 96, 97, 99, 101, 103, 104, 106, 108, 110,
				112, 113, 115, 117, 119, 121, 123, 125, 127, 128
			};

sq_dev_param_t glDeviceParam = {
					STREAMBUF_SIZE,
					OUTPUTBUF_SIZE,
					"flc,pcm,aif,aac,mp3",
					"?",
					"",
					{ 0x00,0x00,0x00,0x00,0x00,0x00 },
					false,
#if defined(RESAMPLE)
					96000,
					true,
					"",
#else
					44100,
#endif
					{ "" },
				} ;


/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/
static pthread_t 	glMainThread;
void				*glConfigID = NULL;
char				glConfigName[SQ_STR_LENGTH] = "./config.xml";
static bool			glDiscovery = false;
u32_t				glScanInterval = SCAN_INTERVAL;
u32_t				glScanTimeout = SCAN_TIMEOUT;
struct sMR			glMRDevices[MAX_RENDERERS];
static struct in_addr glHost;
char				*glHostName;

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
enum { VOLUME_IGNORE = 0, VOLUME_SOFT, VOLUME_HARD };

extern log_level	main_loglevel;
static log_level 	*loglevel = &main_loglevel;

static pthread_t			glUpdateMRThread;
static bool			glMainRunning = true;

static struct mdnsd *gl_mDNSResponder;
static int			glActiveRemoteSock;
static pthread_t	glActiveRemoteThread;

static char usage[] =
			VERSION "\n"
		   "See -t for license terms\n"
		   "Usage: [options]\n"
		   "  -s <server>[:<port>]\tConnect to specified server, otherwise uses autodiscovery to find server\n"
		   "  -b <address>]\tNetwork address to bind to\n"
		   "  -x <config file>\tread config from file (default is ./config.xml)\n"
		   "  -i <config file>\tdiscover players, save <config file> and exit\n"
   		   "  -m <name1;name2...>\texclude from search devices whose model name contains name1 or name 2 ...\n"
		   "  -I \t\t\tauto save config at every network scan\n"
		   "  -f <logfile>\t\tWrite debug to logfile\n"
  		   "  -p <pid file>\t\twrite PID in file\n"
		   "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output|web|main|util|raop, level: error|warn|info|debug|sdebug\n"
#if LINUX || FREEBSD
		   "  -z \t\t\tDaemonize\n"
#endif
		   "  -Z \t\t\tNOT interactive\n"
		   "  -k \t\t\tImmediate exit on SIGQUIT and SIGTERM\n"
		   "  -t \t\t\tLicense terms\n"
		   "\n"
		   "Build options:"
#if LINUX
		   " LINUX"
#endif
#if WIN
		   " WIN"
#endif
#if OSX
		   " OSX"
#endif
#if FREEBSD
		   " FREEBSD"
#endif
#if EVENTFD
		   " EVENTFD"
#endif
#if SELFPIPE
		   " SELFPIPE"
#endif
#if WINEVENT
		   " WINEVENT"
#endif
		   "\n\n";

static char license[] =
		   "This program is free software: you can redistribute it and/or modify\n"
		   "it under the terms of the GNU General Public License as published by\n"
		   "the Free Software Foundation, either version 3 of the License, or\n"
		   "(at your option) any later version.\n\n"
		   "This program is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details.\n\n"
		   "You should have received a copy of the GNU General Public License\n"
		   "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n\n"
	;

/*----------------------------------------------------------------------------*/
/* prototypes */
/*----------------------------------------------------------------------------*/
static void *UpdateMRThread(void *args);
static bool AddRaopDevice(struct sMR *Device, struct mDNSItem_s *data);
static void DelRaopDevice(struct sMR *Device);
static bool IsExcluded(char *Model);

/*----------------------------------------------------------------------------*/
bool sq_callback(sq_dev_handle_t handle, void *caller, sq_action_t action, void *param)
{
	struct sMR *device = caller;
	bool rc = true;

	if (!device)	{
		LOG_ERROR("No caller ID in callback", NULL);
		return false;
	}

	if (action == SQ_ONOFF) {
		device->on = *((bool*) param);

		if (device->on && device->Config.AutoPlay)
			sq_notify(device->SqueezeHandle, device, SQ_PLAY, NULL, &device->on);

		if (!device->on) {
			tRaopReq *Req = malloc(sizeof(tRaopReq));

			pthread_mutex_lock(&device->Mutex);
			QueueFlush(&device->Queue);
			strcpy(Req->Type, "OFF");
			QueueInsert(&device->Queue, Req);
			pthread_cond_signal(&device->Cond);
			pthread_mutex_unlock(&device->Mutex);
		}

		LOG_DEBUG("[%p]: device set on/off %d", caller, device->on);
	}

	if (!device->on && action != SQ_SETNAME && action != SQ_SETSERVER) {
		LOG_DEBUG("[%p]: device off or not controlled by LMS", caller);
		return false;
	}

	LOG_SDEBUG("callback for %s (%d)", device->FriendlyName, action);
	pthread_mutex_lock(&device->Mutex);

	switch (action) {
		case SQ_FINISHED:
			device->LastFlush = gettime_ms();
			device->DiscWait = true;
			device->TrackRunning = false;
			device->TrackDuration = 0;
			break;
		case SQ_STOP: {
			tRaopReq *Req = malloc(sizeof(tRaopReq));

			device->TrackRunning = false;
			device->TrackDuration = 0;
			device->sqState = SQ_STOP;
			// see not in raop_client.h why this 2-stages stop is needed
			raopcl_stop(device->Raop);

			strcpy(Req->Type, "FLUSH");
			QueueInsert(&device->Queue, Req);
			pthread_cond_signal(&device->Cond);
			break;
		}
		case SQ_PAUSE: {
			tRaopReq *Req;

			device->TrackRunning = false;
			device->sqState = SQ_PAUSE;
			// see not in raop_client.h why this 2-stages pause is needed
			raopcl_pause(device->Raop);

			Req = malloc(sizeof(tRaopReq));
			strcpy(Req->Type, "FLUSH");
			QueueInsert(&device->Queue, Req);
			pthread_cond_signal(&device->Cond);
			break;
		}
		case SQ_UNPAUSE: {
			tRaopReq *Req = malloc(sizeof(tRaopReq));

			device->TrackRunning = true;
			device->sqState = SQ_PLAY;
			if (*((unsigned*) param))
				raopcl_start_at(device->Raop, TIME_MS2NTP(*((unsigned*) param)) -
								TS2NTP(raopcl_latency(device->Raop), raopcl_sample_rate(device->Raop)));

			Req->Data.Codec = RAOP_NOCODEC;
			strcpy(Req->Type, "CONNECT");
			QueueInsert(&device->Queue, Req);
			pthread_cond_signal(&device->Cond);
			break;
		}
		case SQ_VOLUME: {
			u32_t Volume = *(u16_t*) param;
			int LMSVolume;

			// first convert to 0..100 value
			for (LMSVolume = 100; Volume < LMSVolumeMap[LMSVolume] && LMSVolume; LMSVolume--);

			if (device->Config.VolumeMode == VOLUME_HARD &&	(device->VolumeStamp + 1000 - gettime_ms() > 0x7fffffff) &&
				(Volume || device->Config.MuteOnPause || sq_get_mode(device->SqueezeHandle) == device->sqState)) {
				tRaopReq *Req = malloc(sizeof(tRaopReq));

				device->Volume = LMSVolume;

				Req->Data.Volume = device->VolumeMapping[device->Volume];
				strcpy(Req->Type, "VOLUME");
				QueueInsert(&device->Queue, Req);
				pthread_cond_signal(&device->Cond);
			} else {
				LOG_INFO("[%p]: volume ignored %u", device, LMSVolume);
			}
			break;
		}
		case SQ_CONNECT: {
			sq_format_t *p = (sq_format_t*) param;
			tRaopReq *Req = malloc(sizeof(tRaopReq));

			device->sqState = SQ_PLAY;

			Req->Data.Codec = RAOP_ALAC;
			strcpy(Req->Type, "CONNECT");
			QueueInsert(&device->Queue, Req);
			pthread_cond_signal(&device->Cond);

			LOG_INFO("[%p]: codec:%c, ch:%d, s:%d, r:%d", device, p->codec,
										p->channels, p->sample_size, p->sample_rate);
			break;
		}
		case SQ_METASEND:
			device->MetadataWait = 5;
			break;
		case SQ_STARTED:
			device->TrackStartTime = *((u32_t*) param);
			device->TrackRunning = true;
			device->TrackElapsed = 0;
			device->MetadataWait = 1;
			device->MetadataHash = 0;
			break;
		case SQ_SETNAME:
			strcpy(device->sq_config.name, (char*) param);
			break;
		case SQ_SETSERVER:
			strcpy(device->sq_config.dynamic.server, inet_ntoa(*(struct in_addr*) param));
			break;
		default:
			break;
	}

	pthread_mutex_unlock(&device->Mutex);
	return rc;
}


/*----------------------------------------------------------------------------*/
static void *PlayerThread(void *args)
{
	struct sMR *Device = (struct sMR*) args;

	Device->Running = true;

	while (Device->Running) {
		tRaopReq *req = GetRequest(&Device->Queue, &Device->Mutex, &Device->Cond, 1000);

		// empty means timeout every sec
		if (!req) {
			u32_t now = gettime_ms();

			LOG_DEBUG("[%p]: tick %u", Device, now);

			if (Device->DiscWait && (Device->LastFlush + Device->Config.IdleTimeout * 1000 - now > 0x7fffffff) ) {
				Device->VolumeReady = !Device->Config.VolumeTrigger;
				LOG_INFO("[%p]: Disconnecting %u", Device, now);
				raopcl_disconnect(Device->Raop);
				Device->DiscWait = false;
			}

			Device->Sane = raopcl_is_sane(Device->Raop) ? 0 : Device->Sane + 1;
			if (Device->Sane > 3) {
				LOG_ERROR("[%p]: broken connection, attempting repair", Device);
				raopcl_repair(Device->Raop);
			}

			// after that, only check what's needed when running
			if (!Device->TrackRunning) continue;

			if (Device->Config.SendMetaData) {
				u32_t Time;

				if (!(Device->TrackElapsed % 5) && ((Time = sq_get_time(Device->SqueezeHandle)) != 0)) {
					Device->TrackElapsed = Time / 1000;
				}
				else Device->TrackElapsed++;

				raopcl_set_progress_ms(Device->Raop, Device->TrackElapsed * 1000,
									   Device->TrackDuration);
			}

			pthread_mutex_lock(&Device->Mutex);
			if (Device->MetadataWait && !--Device->MetadataWait && Device->Config.SendMetaData) {
				sq_metadata_t metadata;
				u32_t hash;
				bool valid;

				pthread_mutex_unlock(&Device->Mutex);

				valid = sq_get_metadata(Device->SqueezeHandle, &metadata, false);

				// not a valid metadata, nothing to update
				if (!valid) {
					Device->MetadataWait = 5;
					sq_free_metadata(&metadata);
					continue;
				}

				// on live stream, gather metadata every 5 seconds
				if (metadata.remote && !metadata.duration) Device->MetadataWait = 5;

				hash = hash32(metadata.title) ^ hash32(metadata.artwork);

				if (Device->MetadataHash != hash) {
					Device->TrackDuration = metadata.duration;
					raopcl_set_daap(Device->Raop, 5, "minm", 's', metadata.title,
													 "asar", 's', metadata.artist,
													 "asal", 's', metadata.album,
													 "asgn", 's', metadata.genre,
													 "astn", 'i', (int) metadata.track);

					if (metadata.artwork && Device->Config.SendCoverArt) {
						char *image = NULL, *contentType = NULL;
						int size = http_fetch(metadata.artwork, &contentType, &image);

						if (size != -1) {
							char *p;

							if ((p = stristr(metadata.artwork, ".png")) != NULL && stristr(contentType, "jpeg")) {
								memcpy(p, ".jpg", 4);
								LOG_INFO("[%p]: wrong file extension %s", Device, metadata.artwork);
							}

							raopcl_set_artwork(Device->Raop, contentType, size - 1, image);
						}

						NFREE(image);
						NFREE(contentType);
					}

					Device->MetadataHash = hash;

					LOG_INFO("[%p]: idx %d\n\tartist:%s\n\talbum:%s\n\ttitle:%s\n"
									"\tgenre:%s\n\tduration:%d.%03d\n\tsize:%d\n\tcover:%s",
									 Device, metadata.index, metadata.artist,
									 metadata.album, metadata.title, metadata.genre,
									 div(metadata.duration, 1000).quot,
									 div(metadata.duration,1000).rem, metadata.file_size,
									 metadata.artwork ? metadata.artwork : "");
				}

				sq_free_metadata(&metadata);
			}
			else pthread_mutex_unlock(&Device->Mutex);

			continue;
		}

		if (!strcasecmp(req->Type, "CONNECT")) {

			LOG_INFO("[%p]: raop connecting ...", Device);

			if (raopcl_connect(Device->Raop, Device->PlayerIP, Device->PlayerPort, req->Data.Codec)) {
				Device->DiscWait = false;
				LOG_INFO("[%p]: raop connected", Device);
			}
			else {
				LOG_ERROR("[%p]: raop failed to connect", Device);
			}
		}

		if (!strcasecmp(req->Type, "FLUSH")) {
			// to handle immediate disconnect when a player sends busy
			if (Device->DiscWait) {
				LOG_INFO("[%p]: disconnecting ...", Device);
				Device->DiscWait = false;
				Device->VolumeReady = !Device->Config.VolumeTrigger;
				raopcl_disconnect(Device->Raop);
			}
			else {
				LOG_INFO("[%p]: flushing ...", Device);
				Device->LastFlush = gettime_ms();
				Device->DiscWait = true;
				raopcl_flush(Device->Raop);
			}
		}

		if (!strcasecmp(req->Type, "OFF")) {
			LOG_INFO("[%p]: processing off", Device);
			Device->VolumeReady = !Device->Config.VolumeTrigger;
			raopcl_disconnect(Device->Raop);
			raopcl_sanitize(Device->Raop);
		}

		if (!strcasecmp(req->Type, "VOLUME") && Device->VolumeReady) {
			LOG_INFO("[%p]: processing volume: %d (%.2f)", Device, Device->Volume, req->Data.Volume);
			raopcl_set_volume(Device->Raop, req->Data.Volume);
		}

		free(req);

	}

	return NULL;
}


/*----------------------------------------------------------------------------*/
static bool RefreshTO(char *UDN)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (glMRDevices[i].InUse && !strcmp(glMRDevices[i].UDN, UDN)) {
			glMRDevices[i].TimeOut = false;
			glMRDevices[i].MissingCount = glMRDevices[i].Config.RemoveCount;
			return true;
		}
	}
	return false;
}


/*----------------------------------------------------------------------------*/
char *GetmDNSAttribute(struct mDNSItem_s *p, char *name)
{
	int j;

	for (j = 0; j < p->attr_count; j++)
		if (!strcasecmp(p->attr[j].name, name))
			return strdup(p->attr[j].value);

	return NULL;
}


/*----------------------------------------------------------------------------*/
static void *UpdateMRThread(void *args)
{
	struct sMR *Device = NULL;
	int i, TimeStamp;
	DiscoveredList DiscDevices;
	static bool Running = false;

	if (Running) return NULL;
	Running = true;

	LOG_DEBUG("Begin Raop devices update", NULL);
	TimeStamp = gettime_ms();

	query_mDNS(gl_mDNSId, "_raop._tcp.local", &DiscDevices, glScanTimeout);

	if (!glMainRunning) {
		LOG_DEBUG("Aborting ...", NULL);
		Running = false;
		return NULL;
	}

	for (i = 0; i < DiscDevices.count; i++) {
		int j;
		struct mDNSItem_s *p = &DiscDevices.items[i];
		char *am = GetmDNSAttribute(p, "am");
		bool excluded = IsExcluded(am);

		NFREE(am);

		if (!p->name || excluded) continue;

		if (!RefreshTO(p->name)) {
			// new device so search a free spot.
			for (j = 0; j < MAX_RENDERERS && glMRDevices[j].InUse; j++);

			// no more room !
			if (j == MAX_RENDERERS) {
				LOG_ERROR("Too many Raop devices", NULL);
				break;
			} else Device = &glMRDevices[j];

			if (AddRaopDevice(Device, p) && !glSaveConfigFile) {
				// create a new slimdevice
				Device->sq_config.soft_volume = (Device->Config.VolumeMode == VOLUME_SOFT);
				Device->SqueezeHandle = sq_reserve_device(Device, &sq_callback);
				if (!*(Device->sq_config.name)) strcpy(Device->sq_config.name, Device->FriendlyName);
				if (!Device->SqueezeHandle || !sq_run_device(Device->SqueezeHandle,
															 Device->Raop, &Device->sq_config)) {
					sq_release_device(Device->SqueezeHandle);
					Device->SqueezeHandle = 0;
					LOG_ERROR("[%p]: cannot create squeezelite instance (%s)", Device, Device->FriendlyName);
					DelRaopDevice(Device);
				}
			}
		} else {
			for (j = 0; j < MAX_RENDERERS; j++) {
				Device = &glMRDevices[j];
				if (Device->InUse && !strcmp(Device->UDN, p->name)) {
					if (p->port != Device->PlayerPort || p->addr.s_addr != Device->PlayerIP.s_addr) {
						LOG_INFO("[%p]: changed ip:port %s:%d", Device, inet_ntoa(p->addr), p->port);

						Device->PlayerPort = p->port;
						Device->PlayerIP = p->addr;

						// replace ip:port piece of credentials
						if (*Device->Config.Credentials) {
							char *token = strchr(Device->Config.Credentials, '@');
							if (token) *token = '\0';
							sprintf(Device->Config.Credentials + strlen(Device->Config.Credentials), "@%s:%d", inet_ntoa(p->addr), p->port);
						}
					}
					break;
				}
			}
		}
	}

	free_discovered_list(&DiscDevices);

	// then walk through the list of devices to remove missing ones
	for (i = 0; i < MAX_RENDERERS; i++) {
		Device = &glMRDevices[i];
		if (!Device->InUse || !Device->Config.RemoveCount) continue;
		if (Device->TimeOut && Device->MissingCount) Device->MissingCount--;
		if (raopcl_is_connected(Device->Raop) || Device->MissingCount) continue;

		LOG_INFO("[%p]: removing renderer (%s)", Device, Device->FriendlyName);
		if (Device->SqueezeHandle) sq_delete_device(Device->SqueezeHandle);
		DelRaopDevice(Device);
	}

	glDiscovery = true;

	if (glAutoSaveConfigFile && !glSaveConfigFile) {
		LOG_DEBUG("Updating configuration %s", glConfigName);
		SaveConfig(glConfigName, glConfigID, false);
	}

	LOG_DEBUG("End Raop devices update %d", gettime_ms() - TimeStamp);
	Running = false;
	return NULL;
}

/*----------------------------------------------------------------------------*/
static void *MainThread(void *args)
{
	unsigned last = gettime_ms();
	u32_t ScanPoll = glScanInterval*1000 + 1;

	while (glMainRunning) {
		int i;
		int elapsed = gettime_ms() - last;

		// reset timeout and re-scan devices
		ScanPoll += elapsed;
		if (glScanInterval && ScanPoll > glScanInterval*1000) {
			pthread_attr_t attr;

			ScanPoll = 0;

			for (i = 0; i < MAX_RENDERERS; i++) {
				glMRDevices[i].TimeOut = true;
				glDiscovery = false;
			}

			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 64*1024);
			pthread_create(&glUpdateMRThread, &attr, &UpdateMRThread, NULL);
			pthread_detach(glUpdateMRThread);
			pthread_attr_destroy(&attr);
		}

		if (glLogFile && glLogLimit != - 1) {
			s32_t size = ftell(stderr);

			if (size > glLogLimit*1024*1024) {
				u32_t Sum, BufSize = 16384;
				u8_t *buf = malloc(BufSize);

				FILE *rlog = fopen(glLogFile, "rb");
				FILE *wlog = fopen(glLogFile, "r+b");
				LOG_DEBUG("Resizing log", NULL);
				for (Sum = 0, fseek(rlog, size - (glLogLimit*1024*1024) / 2, SEEK_SET);
					 (BufSize = fread(buf, 1, BufSize, rlog)) != 0;
					 Sum += BufSize, fwrite(buf, 1, BufSize, wlog));

				Sum = fresize(wlog, Sum);
				fclose(wlog);
				fclose(rlog);
				NFREE(buf);
				if (!freopen(glLogFile, "a", stderr)) {
					LOG_ERROR("re-open error while truncating log", NULL);
				}
			}
		}

		last = gettime_ms();
		sleep(1);
	}
	return NULL;
}


/*----------------------------------------------------------------------------*/
void SetVolumeMapping(struct sMR *Device)
{
	char *p;
	int i = 1;
	float a1 = 1, b1 = -30, a2 = 0, b2 = 0;

	Device->VolumeMapping[0] = -144.0;
	p = Device->Config.VolumeMapping;
	do {
		if (!p || !sscanf(p, "%f:%f", &b2, &a2)) {
			LOG_ERROR("[%p]: wrong volume mapping table", Device, p);
			break;
		}
		p = strchr(p, ',');
		if (p) p++;

		while (i <= a2) {
			Device->VolumeMapping[i] = (a1 == a2) ? b1 :
									   i*(b1-b2)/(a1-a2) + b1 - a1*(b1-b2)/(a1-a2);
			i++;
		}

		a1 = a2;
		b1 = b2;
	} while (i <= 100);

	for (; i <= 100; i++) Device->VolumeMapping[i] = Device->VolumeMapping[i-1];
}


/*----------------------------------------------------------------------------*/
static bool AddRaopDevice(struct sMR *Device, struct mDNSItem_s *data)
{
	pthread_attr_t attr;
	raop_crypto_t Crypto;
	bool Auth = false;
	char *p;
	u32_t mac_size = 6;
	char *am = GetmDNSAttribute(data, "am");
	char Secret[SQ_STR_LENGTH] = "";

	// read parameters from default then config file
	memset(Device, 0, sizeof(struct sMR));
	memcpy(&Device->Config, &glMRConfig, sizeof(tMRConfig));
	memcpy(&Device->sq_config, &glDeviceParam, sizeof(sq_dev_param_t));
	LoadMRConfig(glConfigID, data->name, &Device->Config, &Device->sq_config);

	if (!Device->Config.Enabled) return false;

	// don't create virtual players of devices created by shairtunes2 ...
	if (am && !strcasecmp(am, "shairtunes2")) {
		LOG_DEBUG("[%p]: skipping shairtunes2 player", Device);
		NFREE(am);
		return false;
	}

	 // if airport express, forece auth
	 if (am && stristr(am, "airport")) {
		LOG_INFO("[%p]: AirPort Express", Device);
		Auth = true;
	}

	 if (am && stristr(am, "appletv")) {
		char *pk = GetmDNSAttribute(data, "pk");
		if (pk && *pk) {
			char *token = strchr(Device->Config.Credentials, '@');
			LOG_INFO("[%p]: AppleTV with authentication (pairing must be done separately)", Device);
			if (Device->Config.Credentials[0]) sscanf(Device->Config.Credentials, "%[a-fA-F0-9]", Secret);
			if (token) *token = '\0';
			sprintf(Device->Config.Credentials + strlen(Device->Config.Credentials), "@%s:%d", inet_ntoa(data->addr), data->port);
		}
		NFREE(pk);
	}

	NFREE(am);

	if (stristr(data->name, "AirSonos")) {
		LOG_DEBUG("[%p]: skipping AirSonos player (please use uPnPBridge)", Device);
		return false;
	}

#if 0
	if (!stristr(data->name, "JBL")) {
		printf("ONLY JBL %s\n", data->name);
		return false;
	}
#endif

	strcpy(Device->UDN, data->name);
	Device->Magic = MAGIC;
	Device->TimeOut = false;
	Device->MissingCount = Device->Config.RemoveCount;
	Device->on = false;
	Device->InUse = true;
	Device->SqueezeHandle = 0;
	Device->Running = true;
	// make sure that 1st volume is not missed
	Device->VolumeStamp = gettime_ms() - 2000;
	Device->PlayerIP = data->addr;
	Device->PlayerPort = data->port;
	Device->DiscWait = false;
	Device->TrackDuration = 0;
	Device->TrackRunning = false;
	Device->TrackElapsed = 0;
	Device->VolumeReady = !Device->Config.VolumeTrigger;
	sprintf(Device->ActiveRemote, "%u", hash32(Device->UDN));
	strcpy(Device->FriendlyName, data->hostname);
	p = stristr(Device->FriendlyName, ".local");
	if (p) *p = '\0';
	SetVolumeMapping(Device);

	LOG_INFO("[%p]: adding renderer (%s)", Device, Device->FriendlyName);

	if (!memcmp(Device->sq_config.mac, "\0\0\0\0\0\0", mac_size)) {
		if (SendARP(Device->PlayerIP.s_addr, INADDR_ANY, (u32_t*) Device->sq_config.mac, &mac_size)) {
			u32_t hash = hash32(Device->UDN);

			LOG_ERROR("[%p]: cannot get mac %s, creating fake %x", Device, Device->FriendlyName, hash);
			memcpy(Device->sq_config.mac + 2, &hash, 4);
		}
		memset(Device->sq_config.mac, 0xaa, 2);
	}

	MakeMacUnique(Device);

	// gather RAOP device capabilities, to be matched mater
	Device->SampleSize = GetmDNSAttribute(data, "ss");
	Device->SampleRate = GetmDNSAttribute(data, "sr");
	Device->Channels = GetmDNSAttribute(data, "ch");
	Device->Codecs = GetmDNSAttribute(data, "cn");
	Device->Crypto = GetmDNSAttribute(data, "et");

	if (!Device->Codecs || !strchr(Device->Codecs, '1')) {
		LOG_WARN("[%p]: ALAC not in codecs, player might not work %s", Device, Device->Codecs);
	}

	if ((Device->Config.Encryption || Auth) && strchr(Device->Crypto, '1'))
		Crypto = RAOP_RSA;
	else
		Crypto = RAOP_CLEAR;

	Device->Raop = raopcl_create(glHost, glDACPid, Device->ActiveRemote,
								 RAOP_ALAC, Device->Config.AlacEncode, FRAMES_PER_BLOCK,
								 (u32_t) MS2TS(Device->Config.ReadAhead, Device->SampleRate ? atoi(Device->SampleRate) : 44100),
								 Crypto, Auth, Secret,
								 Device->SampleRate ? atoi(Device->SampleRate) : 44100,
								 Device->SampleSize ? atoi(Device->SampleSize) : 16,
								 Device->Channels ? atoi(Device->Channels) : 2,
								 raopcl_float_volume(Device->Config.Volume));

	if (!Device->Raop) {
		LOG_ERROR("[%p]: cannot create raop device", Device);
		NFREE(Device->SampleSize);
		NFREE(Device->SampleRate);
		NFREE(Device->Channels);
		NFREE(Device->Codecs);
		NFREE(Device->Crypto);
		return false;
	}

	pthread_mutex_init(&Device->Mutex, 0);
	pthread_cond_init(&Device->Cond, 0);
	QueueInit(&Device->Queue);

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 64*1024);
	pthread_create(&Device->Thread, NULL, &PlayerThread, Device);
	pthread_attr_destroy(&attr);

	return true;
}


/*----------------------------------------------------------------------------*/
void FlushRaopDevices(void)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		struct sMR *p = &glMRDevices[i];
		if (p->InUse) DelRaopDevice(p);
	}
}


/*----------------------------------------------------------------------------*/
void DelRaopDevice(struct sMR *Device)
{
	pthread_mutex_lock(&Device->Mutex);
	Device->Running = false;
	Device->InUse = false;
	pthread_cond_signal(&Device->Cond);
	pthread_mutex_unlock(&Device->Mutex);

	pthread_join(Device->Thread, NULL);

	pthread_cond_destroy(&Device->Cond);
	pthread_mutex_destroy(&Device->Mutex);

	raopcl_destroy(Device->Raop);

	LOG_INFO("[%p]: Raop device stopped", Device);

	NFREE(Device->SampleSize);
	NFREE(Device->SampleRate);
	NFREE(Device->Channels);
	NFREE(Device->Codecs);
	NFREE(Device->Crypto);

	memset(Device, 0, sizeof(struct sMR));
}


/*----------------------------------------------------------------------------*/
static void *ActiveRemoteThread(void *args)
{
	int n;
	char buf[1024], command[128], ActiveRemote[16];
	char response[] = "HTTP/1.1 204 No Content\r\nDate: %s,%02d %s %4d %02d:%02d:%02d "
					  "GMT\r\nDAAP-Server: iTunes/7.6.2 (Windows; N;)\r\nContent-Type: "
					  "application/x-dmap-tagged\r\nContent-Length: 0\r\n"
					  "Connection: close\r\n\r\n";
	char *day[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
	char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sept", "Oct", "Nov", "Dec" };

	if (listen(glActiveRemoteSock, 5) < 0) {
		LOG_ERROR("Cannot listen", NULL);
		return NULL;
	}

	while (glMainRunning) {
		int sd, i;
		struct sockaddr cli_addr;
		socklen_t clilen = sizeof(cli_addr);
		struct sMR *Device = NULL;
		char *p;
		time_t now = time(NULL);
		struct tm gmt;

		sd = accept(glActiveRemoteSock, (struct sockaddr *) &cli_addr, &clilen);

		if (sd < 0) {
			if (glMainRunning) {
				LOG_ERROR("Accept error", NULL);
			}
			continue;
		}

		// receive data, all should be in a single receive, hopefully
		n = recv(sd, (void*) buf, sizeof(buf) - 1, 0);
		buf[sizeof(buf) - 1] = '\0';
		strlwr(buf);

		// a pretty basic reading of command
		p = strstr(buf, "active-remote:");
		if (p) sscanf(p, "active-remote:%15s", ActiveRemote);
		ActiveRemote[sizeof(ActiveRemote) - 1] = '\0';

		p = strstr(buf, "/ctrl-int/1/");
		if (p) sscanf(p, "/ctrl-int/1/%127s", command);
		command[sizeof(command) - 1] = '\0';

		for (i = 0; i < MAX_RENDERERS; i++) {
			if (glMRDevices[i].InUse && !strcmp(glMRDevices[i].ActiveRemote, ActiveRemote)) {
				Device = &glMRDevices[i];
				break;
			}
		}

		if (!Device) {
			LOG_ERROR("DACP from unknown player %s", buf);
			continue;
		}

		LOG_INFO("[%p]: remote command %s", Device, command);

		if (!strcasecmp(command, "pause")) sq_notify(Device->SqueezeHandle, Device, SQ_PAUSE, NULL, NULL);
		if (!strcasecmp(command, "play")) sq_notify(Device->SqueezeHandle, Device, SQ_PLAY, NULL, NULL);
		if (!strcasecmp(command, "playpause")) sq_notify(Device->SqueezeHandle, Device, SQ_PLAY_PAUSE, NULL, NULL);
		if (!strcasecmp(command, "stop")) sq_notify(Device->SqueezeHandle, Device, SQ_STOP, NULL, NULL);
		if (!strcasecmp(command, "mutetoggle")) sq_notify(Device->SqueezeHandle, Device, SQ_MUTE_TOGGLE, NULL, NULL);
		if (!strcasecmp(command, "nextitem")) sq_notify(Device->SqueezeHandle, Device, SQ_NEXT, NULL, NULL);
		if (!strcasecmp(command, "previtem")) sq_notify(Device->SqueezeHandle, Device, SQ_PREVIOUS, NULL, NULL);
		if (!strcasecmp(command, "volumeup")) sq_notify(Device->SqueezeHandle, Device, SQ_VOLUME, NULL, "up");
		if (!strcasecmp(command, "volumedown")) sq_notify(Device->SqueezeHandle, Device, SQ_PREVIOUS, NULL, "down");
		if (!strcasecmp(command, "shuffle_songs")) sq_notify(Device->SqueezeHandle, Device, SQ_SHUFFLE, NULL, NULL);
		if (!strcasecmp(command, "beginff") || !strcasecmp(command, "beginrew")) {
			Device->SkipStart = gettime_ms();
			Device->SkipDir = !strcasecmp(command, "beginff") ? true : false;
		}
		if (!strcasecmp(command, "playresume")) {
			s32_t gap = gettime_ms() - Device->SkipStart;
			gap = (gap + 3) * (gap + 3) * (Device->SkipDir ? 1 : -1);
			sq_notify(Device->SqueezeHandle, Device, SQ_FF_REW, NULL, &gap);
		}

		// handle DMCP commands
		if (stristr(command, "setproperty?dmcp.device")) {
			// player is switched to something else, so require immediate disc
			if (stristr(command, "-prevent-playback=1")) {
				Device->DiscWait = true;
				sq_notify(Device->SqueezeHandle, Device,
						!strcasecmp(Device->Config.PreventPlayback, "STOP") ? SQ_STOP : SQ_OFF,
						NULL, NULL);
			}

			// volume remote command
			if (stristr(command, "-volume=")) {
				// need to wait for a 1st setproperty
				if (!Device->VolumeReady) {
					tRaopReq *Req = malloc(sizeof(tRaopReq));

					Device->VolumeReady = true;
					Req->Data.Volume = Device->VolumeMapping[Device->Volume];
					strcpy(Req->Type, "VOLUME");
					QueueInsert(&Device->Queue, Req);
					pthread_cond_signal(&Device->Cond);
				} else if (Device->Config.VolumeMode != VOLUME_SOFT && Device->Config.VolumeFeedback) {
					float volume;
					char vol[10];
					int i;

					Device->VolumeStamp = gettime_ms();
					sscanf(command, "%*[^=]=%f", &volume);
					for (i = 0; i < 100 && volume > Device->VolumeMapping[i]; i++ );
					sprintf(vol, "%d", i);
					LOG_INFO("[%p]: volume feedback %s (%.2f)", Device, vol, volume);
					sq_notify(Device->SqueezeHandle, Device, SQ_VOLUME, NULL, vol);
				}
			}
		}

		// send pre-made response
		gmt = *gmtime(&now);
		sprintf(buf, response, day[gmt.tm_wday], gmt.tm_mday, month[gmt.tm_mon],
								gmt.tm_year + 1900, gmt.tm_hour, gmt.tm_min, gmt.tm_sec);
		n = send(sd, buf, strlen(buf), 0);

#if WIN
		shutdown(sd, SD_BOTH);
#else
		shutdown(sd, SHUT_RDWR);
#endif
		close(sd);
	}

	return NULL;
}

/*----------------------------------------------------------------------------*/
void StartActiveRemote(struct in_addr host)
{
	struct mdns_service *svc;
	struct sockaddr_in my_addr;
	socklen_t nlen = sizeof(struct sockaddr);
	int port;
	char buf[256];
	const char *txt[] = {
		"txtvers=1",
		"Ver=131075",
		"DbId=63B5E5C0C201542E",
		"OSsi=0x1F5",
		NULL
	};

	if ((glActiveRemoteSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		LOG_ERROR("Cannot create ActiveRemote socket", NULL);
		return;
	}

	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_addr.s_addr = host.s_addr;
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = 0;

	if (bind(glActiveRemoteSock, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0) {
		LOG_ERROR("Cannot bind ActiveRemote: %s", strerror(errno));
		return;
	}

	getsockname(glActiveRemoteSock, (struct sockaddr *) &my_addr, &nlen);
	port = ntohs(my_addr.sin_port);
	LOG_INFO("DACP port: %d", port);

	// start mDNS responder
	if ((gl_mDNSResponder = mdnsd_start(host)) == NULL) {
		LOG_ERROR("mdnsd responder start error", NULL);
	}

	sprintf(buf, "%s.local", glHostName);
	mdnsd_set_hostname(gl_mDNSResponder, buf, host);
	sprintf(buf, "iTunes_Ctrl_%s", glDACPid);
	svc = mdnsd_register_svc(gl_mDNSResponder, buf, "_dacp._tcp.local", port, NULL, txt);
	mdns_service_destroy(svc);

	// start ActiveRemote answering thread
	pthread_create(&glActiveRemoteThread, NULL, ActiveRemoteThread, NULL);
}



/*----------------------------------------------------------------------------*/
void StopActiveRemote(void)
{
	if (glActiveRemoteSock != -1) {
#if WIN
		shutdown(glActiveRemoteSock, SD_BOTH);
#else
		shutdown(glActiveRemoteSock, SHUT_RDWR);
#endif
		close(glActiveRemoteSock);
	}
	pthread_join(glActiveRemoteThread, NULL);
	mdnsd_stop(gl_mDNSResponder);
}


/*----------------------------------------------------------------------------*/
bool IsExcluded(char *Model)
{
	char item[SQ_STR_LENGTH];
	char *p = glExcluded;

	while (Model && p && *p && sscanf(p, "%[^;]", item)) {
		if (stristr(Model, item)) return true;
		p += strlen(item) + 1;
	}

	return false;
}


/*----------------------------------------------------------------------------*/
static bool Start(void)
{
	pthread_attr_t attr;

	if (glScanInterval) {
		if (glScanInterval < SCAN_INTERVAL) glScanInterval = SCAN_INTERVAL;
		if (glScanTimeout < SCAN_TIMEOUT) glScanTimeout = SCAN_TIMEOUT;
		if (glScanTimeout > glScanInterval - SCAN_TIMEOUT) glScanTimeout = glScanInterval - SCAN_TIMEOUT;
	}

	memset(&glMRDevices, 0, sizeof(glMRDevices));

	glHost.s_addr = get_localhost(&glHostName);
	if (!strstr(glInterface, "?")) glHost.s_addr = inet_addr(glInterface);

	LOG_INFO("Binding to %s", inet_ntoa(glHost));

	// init mDNS
	gl_mDNSId = init_mDNS(false, glHost);

	// Start the ActiveRemote server
	StartActiveRemote(glHost);

	/* start the main thread */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 64*1024);
	pthread_create(&glMainThread, &attr, &MainThread, NULL);
	pthread_attr_destroy(&attr);

	return true;
}


/*---------------------------------------------------------------------------*/
static bool Stop(void)
{
	pthread_join(glUpdateMRThread, NULL);

	LOG_DEBUG("flush renderers ...", NULL);
	FlushRaopDevices();

	// this forces an ongoing search to end
	LOG_DEBUG("terminate mDNS queries", NULL);
	close_mDNS(gl_mDNSId);

	// Stop ActiveRemote server
	LOG_DEBUG("terminate mDNS responder", NULL);
	StopActiveRemote();

	LOG_DEBUG("terminate main thread ...", NULL);
	pthread_join(glMainThread, NULL);

	free(glHostName);

	return true;
}


/*---------------------------------------------------------------------------*/
static void sighandler(int signum) {
	glMainRunning = false;

	if (!glGracefullShutdown) {
		LOG_INFO("forced exit", NULL);
		exit(EXIT_SUCCESS);
	}

	sq_end();
	Stop();
	exit(EXIT_SUCCESS);
}


/*---------------------------------------------------------------------------*/
bool ParseArgs(int argc, char **argv) {
	char *optarg = NULL;
	int optind = 1;
	int i;

#define MAXCMDLINE 256
	char cmdline[MAXCMDLINE] = "";

	for (i = 0; i < argc && (strlen(argv[i]) + strlen(cmdline) + 2 < MAXCMDLINE); i++) {
		strcat(cmdline, argv[i]);
		strcat(cmdline, " ");
	}

	while (optind < argc && strlen(argv[optind]) >= 2 && argv[optind][0] == '-') {
		char *opt = argv[optind] + 1;
		if (strstr("stxdfpibm", opt) && optind < argc - 1) {
			optarg = argv[optind + 1];
			optind += 2;
		} else if (strstr("tzZIk"
#if defined(RESAMPLE)
						  "uR"
#endif
		  , opt)) {
			optarg = NULL;
			optind += 1;
		}
		else {
			printf("%s", usage);
			return false;
		}

		switch (opt[0]) {
		case 's':
			strcpy(glDeviceParam.server, optarg);
			break;
		case 'b':
			strcpy(glInterface, optarg);
			break;
#if defined(RESAMPLE)
		case 'u':
		case 'R':
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				strcpy(glDeviceParam.resample_options, argv[optind++]);
				glDeviceParam.resample = true;
			} else {
				strcpy(glDeviceParam.resample_options, "");
				glDeviceParam.resample = false;
			}
			break;
#endif
		case 'f':
			glLogFile = optarg;
			break;
		case 'i':
			glSaveConfigFile = optarg;
			break;
		case 'I':
			glAutoSaveConfigFile = true;
			break;
		case 'p':
			glPidFile = optarg;
			break;
		case 'Z':
			glInteractive = false;
			break;
		case 'k':
			glGracefullShutdown = false;
			break;
		case 'm':
			strcpy(glExcluded, optarg);
			break;
#if LINUX || FREEBSD
		case 'z':
			glDaemonize = true;
			break;
#endif
		case 'd':
			{
				char *l = strtok(optarg, "=");
				char *v = strtok(NULL, "=");
				log_level new = lWARN;
				if (l && v) {
					if (!strcmp(v, "error"))  new = lERROR;
					if (!strcmp(v, "warn"))   new = lWARN;
					if (!strcmp(v, "info"))   new = lINFO;
					if (!strcmp(v, "debug"))  new = lDEBUG;
					if (!strcmp(v, "sdebug")) new = lSDEBUG;
					if (!strcmp(l, "all") || !strcmp(l, "slimproto"))	slimproto_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "stream"))    	stream_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "decode"))    	decode_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "output"))    	output_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "main"))     	main_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "util"))    	util_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "raop"))    	raop_loglevel = new;
					if (!strcmp(l, "all") || !strcmp(l, "slimmain"))    slimmain_loglevel = new;				}
				else {
					printf("%s", usage);
					return false;
				}
			}
			break;
		case 't':
			printf("%s", license);
			return false;
		default:
			break;
		}
	}

	return true;
}


/*----------------------------------------------------------------------------*/
/*																			  */
/*----------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	int i;
	char resp[20] = "";

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
#if defined(SIGPIPE)
	signal(SIGPIPE, SIG_IGN);
#endif
#if defined(SIGQUIT)
	signal(SIGQUIT, sighandler);
#endif
#if defined(SIGHUP)
	signal(SIGHUP, sighandler);
#endif

	// first try to find a config file on the command line
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-x")) {
			strcpy(glConfigName, argv[i+1]);
		}
	}

	// load config from xml file
	glConfigID = (void*) LoadConfig(glConfigName, &glMRConfig, &glDeviceParam);

	// potentially overwrite with some cmdline parameters
	if (!ParseArgs(argc, argv)) exit(1);

	if (glLogFile) {
		if (!freopen(glLogFile, "a", stderr)) {
			fprintf(stderr, "error opening logfile %s: %s\n", glLogFile, strerror(errno));
		}
	}

	LOG_ERROR("Starting squeeze2raop version: %s\n", VERSION);

	if (!glConfigID) {
		LOG_ERROR("\n\n!!!!!!!!!!!!!!!!!! ERROR LOADING CONFIG FILE !!!!!!!!!!!!!!!!!!!!!\n", NULL);
	}

#if LINUX || FREEBSD
	if (glDaemonize && !glSaveConfigFile) {
		if (daemon(1, glLogFile ? 1 : 0)) {
			fprintf(stderr, "error daemonizing: %s\n", strerror(errno));
		}
	}
#endif

	if (glPidFile) {
		FILE *pid_file;
		pid_file = fopen(glPidFile, "wb");
		if (pid_file) {
			fprintf(pid_file, "%d", getpid());
			fclose(pid_file);
		}
		else {
			LOG_ERROR("Cannot open PID file %s", glPidFile);
		}
	}

	sq_init();

	if (!Start()) {
		LOG_ERROR("Cannot start", NULL);
		strcpy(resp, "exit");
	}

	if (glSaveConfigFile) {
		while (!glDiscovery) sleep(1);
		SaveConfig(glSaveConfigFile, glConfigID, true);
	}

	while (strcmp(resp, "exit") && !glSaveConfigFile) {

#if LINUX || FREEBSD
		if (!glDaemonize && glInteractive)
			i = scanf("%s", resp);
		else
			pause();
#else
		if (glInteractive)
			i = scanf("%s", resp);
		else
#if OSX
			pause();
#else
			Sleep(INFINITE);
#endif
#endif

	if (!strcmp(resp, "streamdbg"))	{
			char level[20];
			i = scanf("%s", level);
			stream_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "outputdbg"))	{
			char level[20];
			i = scanf("%s", level);
			output_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "decodedbg"))	{
			char level[20];
			i = scanf("%s", level);
			decode_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "slimprotodbg"))	{
			char level[20];
			i = scanf("%s", level);
			slimproto_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "maindbg"))	{
			char level[20];
			i = scanf("%s", level);
			main_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "slimmaindbg"))	{
			char level[20];
			i = scanf("%s", level);
			slimmain_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "utildbg"))	{
			char level[20];
			i = scanf("%s", level);
			util_loglevel = debug2level(level);
		}

		if (!strcmp(resp, "raopdbg"))	{
			char level[20];
			i = scanf("%s", level);
			raop_loglevel = debug2level(level);
		}
		 if (!strcmp(resp, "save"))	{
			char name[128];
			i = scanf("%s", name);
			SaveConfig(name, glConfigID, true);
		}
	}

	if (glConfigID) ixmlDocument_free(glConfigID);
	glMainRunning = false;
	LOG_INFO("stopping squeelite devices ...", NULL);
	sq_end();
	LOG_INFO("stopping Raop devices ...", NULL);
	Stop();
	LOG_INFO("all done", NULL);

	return true;
}




