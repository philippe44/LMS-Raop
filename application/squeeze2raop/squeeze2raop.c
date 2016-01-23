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
#include "util_common.h"
#include "util.h"
#include "raop_itf.h"
#include "raop_util.h"
#include "mdnssd-itf.h"

/*
TODO :
- for no pause, the solution will be to send the elapsed time to LMS through CLI so that it does take care of the seek
- samplerate management will have to be reviewed when decode will be used
*/

/*----------------------------------------------------------------------------*/
/* globals initialized */
/*----------------------------------------------------------------------------*/
char				glSQServer[SQ_STR_LENGTH] = "?";
char				glInterface[16] = "?";
#if LINUX || FREEBSD
bool				glDaemonize = false;
#endif
bool				glInteractive = true;
char				*glLogFile;
s32_t				glLogLimit = -1;
static char			*glPidFile = NULL;
static char			*glSaveConfigFile = NULL;
bool				glAutoSaveConfigFile = false;
bool				glGracefullShutdown = true;
int					gl_mDNSId;

log_level	slimproto_loglevel = lWARN;
log_level	stream_loglevel = lWARN;
log_level	decode_loglevel = lWARN;
log_level	output_loglevel = lWARN;
log_level	main_loglevel = lINFO;
log_level	slimmain_loglevel = lINFO;
log_level	util_loglevel = lWARN;
log_level	raop_loglevel = lINFO;

tMRConfig			glMRConfig = {
							true,
							"",
							true,
							true,
							3,
							false,
							30,
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
					"pcm",
					SQ_RATE_44100,
					16,
					{ 0x00,0x00,0x00,0x00,0x00,0x00 },
					false,
					1000,
				} ;

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/
static pthread_t 	glMainThread;
unsigned int 		glPort;
char 				glIPaddress[16] = "?";
void				*glConfigID = NULL;
char				glConfigName[SQ_STR_LENGTH] = "./config.xml";
static bool			glDiscovery = false;
u32_t				glScanInterval = SCAN_INTERVAL;
u32_t				glScanTimeout = SCAN_TIMEOUT;
struct sMR			glMRDevices[MAX_RENDERERS];

/*----------------------------------------------------------------------------*/
/* consts or pseudo-const*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
extern log_level	main_loglevel;
static log_level 	*loglevel = &main_loglevel;

pthread_t			glUpdateMRThread;
static bool			glMainRunning = true;

static char usage[] =
			VERSION "\n"
		   "See -t for license terms\n"
		   "Usage: [options]\n"
		   "  -s <server>[:<port>]\tConnect to specified server, otherwise uses autodiscovery to find server\n"
		   "  -x <config file>\tread config from file (default is ./config.xml)\n"
		   "  -i <config file>\tdiscover players, save <config file> and exit\n"
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
void 		DelRaopDevice(struct sMR *Device);

/*----------------------------------------------------------------------------*/
bool sq_callback(sq_dev_handle_t handle, void *caller, sq_action_t action, u8_t *cookie, void *param)
{
	struct sMR *device = caller;
	char *p = (char*) param;
	bool rc = true;

	if (!device)	{
		LOG_ERROR("No caller ID in callback", NULL);
		return false;
	}

	if (action == SQ_ONOFF) {
		device->on = *((bool*) param);

		if (device->on && device->Config.AutoPlay)
			sq_notify(device->SqueezeHandle, device, SQ_PLAY, NULL, &device->on);

		if (!device->on) RaopDisconnect(device->RaopCtx);

		LOG_DEBUG("[%p]: device set on/off %d", caller, device->on);
	}

	if (!device->on) {
		LOG_DEBUG("[%p]: device off or not controlled by LMS", caller);
		return false;
	}

	LOG_SDEBUG("callback for %s (%d)", device->FriendlyName, action);
	pthread_mutex_lock(&device->Mutex);

	switch (action) {
		case SQ_STOP:
			RaopStop(device->RaopCtx);
			break;
		case SQ_PAUSE:
			RaopPause(device->RaopCtx);
			break;
		case SQ_UNPAUSE:
			RaopUnPause(device->RaopCtx);
			break;
		case SQ_VOLUME: {
			u32_t Volume = *(u16_t*)p;
			int i;

			for (i = 100; Volume < LMSVolumeMap[i] && i; i--);
			device->Volume = i;
			RaopSetVolume(device->RaopCtx, device->Volume);

			break;
		}
		case SQ_CONNECT: {
			sq_format_t *p = (sq_format_t*) param;

			LOG_INFO("[%p]: codec:%c, ch:%d, s:%d, r:%d", device, p->codec,
										p->channels, p->sample_size, p->sample_rate);

			 // FIXME: will have to change if at some point other codecs than PCM are supported
			if (p->sample_size != atoi(device->RaopCap.SampleSize)) rc = false;
			if (p->sample_rate != atoi(device->RaopCap.SampleRate)) rc = false;
			if (p->codec == 'p' && !strchr(device->RaopCap.Codecs, '0') && !strchr(device->RaopCap.Codecs, '1')) rc = false;
			if (p->codec == 'l' && !strchr(device->RaopCap.Codecs, '1')) rc = false;
			if (p->codec == 'a' && !strchr(device->RaopCap.Codecs, '2') && !strchr(device->RaopCap.Codecs, '3')) rc = false;

			// this is where we can device to flush & re-start the RTSP connection
			if (rc) {
				//sq_get_metadata(device->SqueezeHandle, &device->MetaData, false);
				//sq_free_metadata(&device->MetaData);
				rc = RaopConnect(device->RaopCtx, RAOP_ALAC, &device->MetaData);
			}
			else {
				LOG_ERROR("[%p]: invalid codec settings", device);
			}
			break;
		}
		default:
			break;
	}

	pthread_mutex_unlock(&device->Mutex);
	return rc;
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

	LOG_DEBUG("Begin Cast devices update", NULL);
	TimeStamp = gettime_ms();

	if (!glMainRunning) {
		LOG_DEBUG("Aborting ...", NULL);
		return NULL;
	}

	query_mDNS(gl_mDNSId, "_raop._tcp.local", &DiscDevices, glScanTimeout);

	for (i = 0; i < DiscDevices.count; i++) {
		int j;
		struct mDNSItem_s *p = &DiscDevices.items[i];

		if (!p->name) continue;

		if (!RefreshTO(p->name)) {
			// new device so search a free spot.
			for (j = 0; j < MAX_RENDERERS && glMRDevices[j].InUse; j++);

			// no more room !
			if (j == MAX_RENDERERS) {
				LOG_ERROR("Too many Raop devices", NULL);
				break;
			}
			else Device = &glMRDevices[j];

			if (AddRaopDevice(Device, p) && !glSaveConfigFile) {
				// create a new slimdevice
				Device->sq_config.sample_rate = atoi(Device->RaopCap.SampleRate);
				Device->sq_config.sample_size = atoi(Device->RaopCap.SampleSize);
				Device->SqueezeHandle = sq_reserve_device(Device, &sq_callback);
				if (!Device->SqueezeHandle || !sq_run_device(Device->SqueezeHandle,
					GetRaopcl(Device->RaopCtx),
					*(Device->Config.Name) ? Device->Config.Name : Device->FriendlyName,
					&Device->sq_config)) {
					sq_release_device(Device->SqueezeHandle);
					Device->SqueezeHandle = 0;
					LOG_ERROR("[%p]: cannot create squeezelite instance (%s)", Device, Device->FriendlyName);
					DelRaopDevice(Device);
				}
			}
		}
		else {
			for (j = 0; j < MAX_RENDERERS; j++) {
				if (glMRDevices[j].InUse && !strcmp(glMRDevices[j].UDN, p->name)) {
					UpdateRaopPort(glMRDevices[j].RaopCtx, p->port);
					break;
				}
			}
		}
	}

	free_discovered_list(&DiscDevices);

	// then walk through the list of devices to remove missing ones
	for (i = 0; i < MAX_RENDERERS; i++) {
		Device = &glMRDevices[i];
		if (!Device->InUse) continue;
		if (Device->TimeOut && Device->MissingCount) Device->MissingCount--;
		if (Device->MissingCount) continue;

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
			pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 32*1024);
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
static bool AddRaopDevice(struct sMR *Device, struct mDNSItem_s *data)
{
	raop_crypto_t Crypto;
	char *p;

	// read parameters from default then config file
	memset(Device, 0, sizeof(struct sMR));
	memcpy(&Device->Config, &glMRConfig, sizeof(tMRConfig));
	memcpy(&Device->sq_config, &glDeviceParam, sizeof(sq_dev_param_t));
	LoadMRConfig(glConfigID, data->name, &Device->Config, &Device->sq_config);

	if (!Device->Config.Enabled) return false;

	/*
	if (!stristr(data->name, "jbl")) {
		printf("ONLY JBL %s\n", data->name);
		return false;
	}
	*/

	pthread_mutex_init(&Device->Mutex, 0);
	strcpy(Device->UDN, data->name);
	Device->Magic = MAGIC;
	Device->TimeOut = false;
	Device->MissingCount = Device->Config.RemoveCount;
	Device->on = false;
	Device->SqueezeHandle = 0;
	Device->Running = true;
	Device->InUse = true;
	Device->VolumeStamp = 0;
	Device->ip = data->addr;
	strcpy(Device->FriendlyName, data->hostname);
	p = stristr(Device->FriendlyName, ".local");
	if (p) *p = '\0';

	LOG_INFO("[%p]: adding renderer (%s)", Device, Device->FriendlyName);

	if (!memcmp(Device->sq_config.mac, "\0\0\0\0\0\0", 6)) {
		u32_t hash = hash32(data->name);

		memset(Device->sq_config.mac, 0xaa, 2);
		memcpy(Device->sq_config.mac + 2, &hash, 4);
	}

	// gather RAOP device capabilities, to be macthed mater
	Device->RaopCap.SampleSize = GetmDNSAttribute(data, "ss");
	Device->RaopCap.SampleRate = GetmDNSAttribute(data, "sr");
	Device->RaopCap.Channels = GetmDNSAttribute(data, "ch");
	Device->RaopCap.Codecs = GetmDNSAttribute(data, "cn");
	Device->RaopCap.Crypto = GetmDNSAttribute(data, "et");

	if (!strchr(Device->RaopCap.Codecs, '0')) {
		LOG_ERROR("[%p]: incompatible codec", Device->RaopCap.Codecs);
		return false;
	}

#if 1
	if (strchr(Device->RaopCap.Crypto, '1')) Crypto = RAOP_RSA;
	else Crypto = RAOP_CLEAR;
#else
	Crypto = RAOP_RSA;
#endif

	Device->RaopCtx = CreateRaopDevice(p, glInterface, Device->ip, data->port,
									  RAOP_ALAC, Crypto,
									  Device->Config.IdleTimeout * 1000,
									  atoi(Device->RaopCap.SampleRate),
									  atoi(Device->RaopCap.SampleSize),atoi(Device->RaopCap.Channels),
									  Device->sq_config.player_volume);

	if (!Device->RaopCtx) {
		LOG_ERROR("[%p]: cannot create raop device", Device);
		return false;
	}

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
	pthread_mutex_unlock(&Device->Mutex);
	pthread_join(Device->Thread, NULL);

	DestroyRaopDevice(Device->RaopCtx);
	NFREE(Device->CurrentURI);
	NFREE(Device->NextURI);
	NFREE(Device->RaopCap.SampleSize);
	NFREE(Device->RaopCap.SampleRate);
	NFREE(Device->RaopCap.Channels);
	NFREE(Device->RaopCap.Codecs);
	NFREE(Device->RaopCap.Crypto);

	pthread_mutex_destroy(&Device->Mutex);
	memset(Device, 0, sizeof(struct sMR));
}

/*----------------------------------------------------------------------------*/
static bool Start(void)
{
	if (glScanInterval) {
		if (glScanInterval < SCAN_INTERVAL) glScanInterval = SCAN_INTERVAL;
		if (glScanTimeout < SCAN_TIMEOUT) glScanTimeout = SCAN_TIMEOUT;
		if (glScanTimeout > glScanInterval - SCAN_TIMEOUT) glScanTimeout = glScanInterval - SCAN_TIMEOUT;
	}

	memset(&glMRDevices, 0, sizeof(glMRDevices));

	// init mDNS
	if (strstr(glIPaddress, "?")) gl_mDNSId = init_mDNS(false, NULL);
	else gl_mDNSId = init_mDNS(false, glIPaddress);

	/* start the main thread */
	pthread_create(&glMainThread, NULL, &MainThread, NULL);
	return true;
}


/*---------------------------------------------------------------------------*/
static bool Stop(void)
{
	pthread_join(glUpdateMRThread, NULL);

	LOG_DEBUG("flush renderers ...", NULL);
	FlushRaopDevices();

	// this forces an ongoing search to end
	close_mDNS(gl_mDNSId);

	LOG_DEBUG("terminate main thread ...", NULL);
	pthread_join(glMainThread, NULL);

	return true;
}


/*---------------------------------------------------------------------------*/
static void sighandler(int signum) {
	int i;

	glMainRunning = false;

	if (!glGracefullShutdown) {
		for (i = 0; i < MAX_RENDERERS; i++) {
			struct sMR *p = &glMRDevices[i];
			if (p->InUse) RaopStop(p->RaopCtx);
		}
		LOG_INFO("forced exit", NULL);
		exit(EXIT_SUCCESS);
	}

	sq_stop();
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
		if (strstr("stxdfpi", opt) && optind < argc - 1) {
			optarg = argv[optind + 1];
			optind += 2;
		} else if (strstr("tzZIk"
#if RESAMPLE
						  "uR"
#endif
#if DSD
						  "D"
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
			strcpy(glSQServer, optarg);
			break;
#if RESAMPLE
		case 'u':
		case 'R':
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				gl_resample = argv[optind++];
			} else {
				gl_resample = "";
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

#if LINUX || FREEBSD
		case 'z':
			glDaemonize = true;
			break;
#endif
#if DSD
		case 'D':
			gl_dop = true;
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				gl_dop_delay = atoi(argv[optind++]);
			}
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

	if (strstr(glSQServer, "?")) sq_init(NULL);
	else sq_init(glSQServer);

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

		if (!strcmp(resp, "slimmainqdbg"))	{
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
	sq_stop();
	LOG_INFO("stopping Raop devices ...", NULL);
	Stop();
	LOG_INFO("all done", NULL);

	return true;
}




