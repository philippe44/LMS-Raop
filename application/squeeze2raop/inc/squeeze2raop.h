/*
 *  Squeeze2cast - LMS to Cast gateway
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

#ifndef __SQUEEZE2CAST_H
#define __SQUEEZE2CAST_H

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

#include "pthread.h"
#include "squeezedefs.h"
#include "squeezeitf.h"
#include "raop_client.h"

/*----------------------------------------------------------------------------*/
/* typedefs */
/*----------------------------------------------------------------------------*/

#define MAX_PROTO		128
#define MAX_RENDERERS	32
#define	AV_TRANSPORT 	"urn:schemas-upnp-org:service:AVTransport:1"
#define	RENDERING_CTRL 	"urn:schemas-upnp-org:service:RenderingControl:1"
#define	CONNECTION_MGR 	"urn:schemas-upnp-org:service:ConnectionManager:1"
#define MAGIC			0xAABBCCDD
#define RESOURCE_LENGTH	250
#define	SCAN_TIMEOUT 	15
#define SCAN_INTERVAL	30


typedef struct sMRConfig
{
	bool		Enabled;			//
	char		Name[SQ_STR_LENGTH];
	bool		SendMetaData;
	bool		SendCoverArt;
	int			RemoveCount;
	bool		AutoPlay;
	int			IdleTimeout;
	bool		Encryption;
} tMRConfig;


struct sMR {
	u32_t Magic;
	bool  InUse;
	tMRConfig Config;
	sq_dev_param_t	sq_config;
	bool on;
	char UDN			[RESOURCE_LENGTH];
	char FriendlyName	[RESOURCE_LENGTH];
	struct in_addr 		ip;
	char			*CurrentURI;
	char			*NextURI;
	char			ContentType[SQ_STR_LENGTH];		// a bit patchy ... to buffer next URI
	sq_metadata_t	MetaData;
	bool			TimeOut;
	int	 			SqueezeHandle;
	pthread_mutex_t Mutex;
	pthread_t 		Thread;
	u8_t			Volume;
	u32_t			VolumeStamp;
	int				MissingCount;
	bool			Running;
	struct sRaopCtx *RaopCtx;
	struct {
		char *SampleSize;
		char *SampleRate;
		char *Channels;
		char *Codecs;
		char *Crypto;
   } RaopCap;
};

struct sAction	{
	sq_dev_handle_t Handle;
	struct sMR		*Caller;
	sq_action_t 	Action;
	u8_t 			*Cookie;
	union {
		u32_t	Volume;
		u32_t	Time;
	} 				Param;
	struct sAction	*Next;
	bool			Ordered;
};

extern char 				glInterface[];
extern u8_t		   			glMac[6];
extern s32_t				glLogLimit;
extern tMRConfig			glMRConfig;
extern sq_dev_param_t		glDeviceParam;
extern char					glSQServer[SQ_STR_LENGTH];
extern u32_t				glScanInterval;
extern u32_t				glScanTimeout;
extern struct sMR			glMRDevices[MAX_RENDERERS];


#endif
