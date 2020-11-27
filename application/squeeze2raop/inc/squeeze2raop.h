/*
 *  Squeeze2Raop - LMS to Raop gateway
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

#ifndef __SQUEEZE2RAOP_H
#define __SQUEEZE2RAOP_H

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

#include "pthread.h"
#include "squeezedefs.h"
#include "squeezeitf.h"
#include "raop_client.h"
#include "util.h"

/*----------------------------------------------------------------------------*/
/* typedefs */
/*----------------------------------------------------------------------------*/

#define MAX_PROTO		128
#define MAX_RENDERERS	32
#define MAGIC			0xAABBCCDD
#define RESOURCE_LENGTH	250
#define	SCAN_TIMEOUT 	15
#define SCAN_INTERVAL	30

#define PLAYER_LATENCY	1500

enum { CONFIG_CREATE, CONFIG_UPDATE, CONFIG_MIGRATE };

typedef struct sRaopReq {
	char Type[20];
	union {
		float Volume;
		u64_t FlushTS;
	} Data;
} tRaopReq;

typedef struct sMRConfig
{
	bool		Enabled;
	bool		SendMetaData;
	bool		SendCoverArt;
	bool		AutoPlay;
	int			IdleTimeout;
	int			RemoveTimeout;
	bool		Encryption;
	char		Credentials[_STR_LEN_];
	int 		ReadAhead;
	int			VolumeMode;
	int			Volume;
	int			VolumeFeedback;
	char		VolumeMapping[_STR_LEN_];
	bool		MuteOnPause;
	bool		AlacEncode;
	bool		VolumeTrigger;
	char 		PreventPlayback[_STR_LEN_];
} tMRConfig;


struct sMR {
	u32_t Magic;
	bool  Running;
	u32_t Expired;
	tMRConfig Config;
	sq_dev_param_t	sq_config;
	bool on;
	char UDN			[RESOURCE_LENGTH];
	char FriendlyName	[RESOURCE_LENGTH];
	char			ContentType[_STR_LEN_];		// a bit patchy ... to buffer next URI
	int	 			SqueezeHandle;
	sq_action_t		sqState;
	s8_t			Volume;
	u32_t			VolumeStampRx, VolumeStampTx;
	float 			VolumeMapping[101];
	bool			VolumeReady;
	u8_t			VolumeReadyWait;
	struct raopcl_s	*Raop;
	struct in_addr 	PlayerIP;
	u16_t			PlayerPort;
	pthread_t		Thread;
	pthread_mutex_t Mutex;
	pthread_cond_t	Cond;
	bool			Delete;
	u32_t			Busy;
	tQueue			Queue;
	u32_t 			LastFlush;
	bool			DiscWait;
	int				Sane;
	bool			TrackRunning;
	u8_t			MetadataWait;
	u32_t			MetadataHash;
	char *SampleSize;
	char *SampleRate;
	char *Channels;
	char *Codecs;
	char *Crypto;
	char ActiveRemote[16];
	u32_t SkipStart;
	bool SkipDir;
};

extern char 				glInterface[];
extern s32_t				glLogLimit;
extern tMRConfig			glMRConfig;
extern sq_dev_param_t		glDeviceParam;
extern struct sMR			glMRDevices[MAX_RENDERERS];
extern char					glExcluded[_STR_LEN_];
extern int					glMigration;
extern char					glPortOpen[_STR_LEN_];

#endif
