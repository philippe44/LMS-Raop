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

#include <stdarg.h>

#include "conf_util.h"
#include "log_util.h"

/*----------------------------------------------------------------------------*/
/* globals */
/*----------------------------------------------------------------------------*/

extern log_level	slimproto_loglevel;
extern log_level	stream_loglevel;
extern log_level	decode_loglevel;
extern log_level	output_loglevel;
extern log_level	main_loglevel;
extern log_level	slimmain_loglevel;
extern log_level	util_loglevel;
extern log_level	raop_loglevel;


/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
extern log_level	util_loglevel;
static log_level 	*loglevel = &util_loglevel;

/*----------------------------------------------------------------------------*/
void MakeMacUnique(struct sMR *Device)
{
	int i;

	for (i = 0; i < MAX_RENDERERS; i++) {
		if (!glMRDevices[i].InUse || Device == &glMRDevices[i]) continue;
		if (!memcmp(&glMRDevices[i].sq_config.mac, &Device->sq_config.mac, 6)) {
			u32_t hash = hash32(Device->UDN);

			LOG_INFO("[%p]: duplicated mac ... updating", Device);
			memset(&Device->sq_config.mac[0], 0xcc, 2);
			memcpy(&Device->sq_config.mac[0] + 2, &hash, 4);
		}
	}
}

/*----------------------------------------------------------------------------*/
void SaveConfig(char *name, void *ref, bool full)
{
	struct sMR *p;
	IXML_Document *doc = ixmlDocument_createDocument();
	IXML_Document *old_doc = ref;
	IXML_Node	 *root, *common;
	IXML_NodeList *list;
	IXML_Element *old_root;
	char *s;
	FILE *file;
	int i;

	old_root = ixmlDocument_getElementById(old_doc, "squeeze2raop");

	if (!full && old_doc) {
		ixmlDocument_importNode(doc, (IXML_Node*) old_root, true, &root);
		ixmlNode_appendChild((IXML_Node*) doc, root);

		list = ixmlDocument_getElementsByTagName((IXML_Document*) root, "device");
		for (i = 0; i < (int) ixmlNodeList_length(list); i++) {
			IXML_Node *device;

			device = ixmlNodeList_item(list, i);
			ixmlNode_removeChild(root, device, &device);
			ixmlNode_free(device);
		}
		if (list) ixmlNodeList_free(list);
		common = (IXML_Node*) ixmlDocument_getElementById((IXML_Document*) root, "common");
	}
	else {
		root = XMLAddNode(doc, NULL, "squeeze2raop", NULL);
		common = (IXML_Node*) XMLAddNode(doc, root, "common", NULL);
	}

	XMLUpdateNode(doc, root, false, "interface", glInterface);
	XMLUpdateNode(doc, root, false, "slimproto_log", level2debug(slimproto_loglevel));
	XMLUpdateNode(doc, root, false, "stream_log", level2debug(stream_loglevel));
	XMLUpdateNode(doc, root, false, "output_log", level2debug(output_loglevel));
	XMLUpdateNode(doc, root, false, "decode_log", level2debug(decode_loglevel));
	XMLUpdateNode(doc, root, false, "main_log",level2debug(main_loglevel));
	XMLUpdateNode(doc, root, false, "slimmain_log", level2debug(slimmain_loglevel));
	XMLUpdateNode(doc, root, false, "raop_log",level2debug(raop_loglevel));
	XMLUpdateNode(doc, root, false, "util_log",level2debug(util_loglevel));
	XMLUpdateNode(doc, root, false, "scan_interval", "%d", (u32_t) glScanInterval);
	XMLUpdateNode(doc, root, false, "scan_timeout", "%d", (u32_t) glScanTimeout);
	XMLUpdateNode(doc, root, false, "log_limit", "%d", (s32_t) glLogLimit);

	XMLUpdateNode(doc, common, false, "streambuf_size", "%d", (u32_t) glDeviceParam.stream_buf_size);
	XMLUpdateNode(doc, common, false, "output_size", "%d", (u32_t) glDeviceParam.output_buf_size);
	XMLUpdateNode(doc, common, false, "enabled", "%d", (int) glMRConfig.Enabled);
	XMLUpdateNode(doc, common, false, "codecs", glDeviceParam.codecs);
	XMLUpdateNode(doc, common, false, "sample_rate", "%d", (int) glDeviceParam.sample_rate);
#if defined(RESAMPLE)
	XMLUpdateNode(doc, common, false, "resample", "%d", (int) glDeviceParam.resample);
	XMLUpdateNode(doc, common, false, "resample_options", glDeviceParam.resample_options);
#endif
	XMLUpdateNode(doc, common, false, "player_volume", "%d", (int) glMRConfig.Volume);
	XMLUpdateNode(doc, common, false, "volume_mapping", glMRConfig.VolumeMapping);
	XMLUpdateNode(doc, common, false, "volume_feedback", "%d", (int) glMRConfig.VolumeFeedback);
	XMLUpdateNode(doc, common, false, "volume_mode", "%d", (int) glMRConfig.VolumeMode);
	XMLUpdateNode(doc, common, false, "mute_on_pause", "%d", (int) glMRConfig.MuteOnPause);
	XMLUpdateNode(doc, common, false, "send_metadata", "%d", (int) glMRConfig.SendMetaData);
	XMLUpdateNode(doc, common, false, "send_coverart", "%d", (int) glMRConfig.SendCoverArt);
	XMLUpdateNode(doc, common, false, "remove_count", "%d", (u32_t) glMRConfig.RemoveCount);
	XMLUpdateNode(doc, common, false, "auto_play", "%d", (int) glMRConfig.AutoPlay);
	XMLUpdateNode(doc, common, false, "idle_timeout", "%d", (int) glMRConfig.IdleTimeout);
	XMLUpdateNode(doc, common, false, "alac_encode", "%d", (int) glMRConfig.AlacEncode);
	XMLUpdateNode(doc, common, false, "volume_trigger", "%d", (int) glMRConfig.VolumeTrigger);
	XMLUpdateNode(doc, common, false, "prevent_playback", glMRConfig.PreventPlayback);

	XMLUpdateNode(doc, common, false, "encryption", "%d", (int) glMRConfig.Encryption);
	XMLUpdateNode(doc, common, false, "read_ahead", "%d", (int) glMRConfig.ReadAhead);
	XMLUpdateNode(doc, common, false, "server", glDeviceParam.server);

	// correct some buggy parameters
	if (glDeviceParam.sample_rate < 44100) XMLUpdateNode(doc, common, true, "sample_rate", "%d", 96000);

	for (i = 0; i < MAX_RENDERERS; i++) {
		IXML_Node *dev_node;

		if (!glMRDevices[i].InUse) continue;
		else p = &glMRDevices[i];

		// existing device, keep param and update "name" if LMS has requested it
		if (old_doc && ((dev_node = (IXML_Node*) FindMRConfig(old_doc, p->UDN)) != NULL)) {
			ixmlDocument_importNode(doc, dev_node, true, &dev_node);
			ixmlNode_appendChild((IXML_Node*) root, dev_node);

			XMLUpdateNode(doc, dev_node, false, "friendly_name", p->FriendlyName);
			XMLUpdateNode(doc, dev_node, true, "name", p->sq_config.name);
			if (*p->Config.Credentials) XMLUpdateNode(doc, dev_node, true, "credentials", p->Config.Credentials);
			if (*p->sq_config.dynamic.server) XMLUpdateNode(doc, dev_node, true, "server", p->sq_config.dynamic.server);
		}
		// new device, add nodes
		else {
			dev_node = XMLAddNode(doc, root, "device", NULL);
			XMLAddNode(doc, dev_node, "udn", p->UDN);
			XMLAddNode(doc, dev_node, "name", p->FriendlyName);
			XMLAddNode(doc, dev_node, "friendly_name", p->FriendlyName);
			if (*p->Config.Credentials) XMLAddNode(doc, dev_node, "credentials", p->Config.Credentials);
			if (*p->sq_config.dynamic.server) XMLAddNode(doc, dev_node, "server", p->sq_config.dynamic.server);
			XMLAddNode(doc, dev_node, "mac", "%02x:%02x:%02x:%02x:%02x:%02x", p->sq_config.mac[0],
						p->sq_config.mac[1], p->sq_config.mac[2], p->sq_config.mac[3], p->sq_config.mac[4], p->sq_config.mac[5]);
			XMLAddNode(doc, dev_node, "enabled", "%d", (int) p->Config.Enabled);
		}
	}

	// add devices in old XML file that has not been discovered
	list = ixmlDocument_getElementsByTagName((IXML_Document*) old_root, "device");
	for (i = 0; i < (int) ixmlNodeList_length(list); i++) {
		char *udn;
		IXML_Node *device, *node;

		device = ixmlNodeList_item(list, i);
		node = (IXML_Node*) ixmlDocument_getElementById((IXML_Document*) device, "udn");
		node = ixmlNode_getFirstChild(node);
		udn = (char*) ixmlNode_getNodeValue(node);
		if (!FindMRConfig(doc, udn)) {
			ixmlDocument_importNode(doc, (IXML_Node*) device, true, &device);
			ixmlNode_appendChild((IXML_Node*) root, device);
		}
	}
	if (list) ixmlNodeList_free(list);

	file = fopen(name, "wb");
	s = ixmlDocumenttoString(doc);
	fwrite(s, 1, strlen(s), file);
	fclose(file);
	free(s);

	ixmlDocument_free(doc);
}


/*----------------------------------------------------------------------------*/
static void LoadConfigItem(tMRConfig *Conf, sq_dev_param_t *sq_conf, char *name, char *val)
{
	if (!val)return;

	if (!strcmp(name, "streambuf_size")) sq_conf->stream_buf_size = atol(val);
	if (!strcmp(name, "output_size")) sq_conf->output_buf_size = atol(val);
	if (!strcmp(name, "codecs")) strcpy(sq_conf->codecs, val);
	if (!strcmp(name, "sample_rate")) sq_conf->sample_rate = atol(val);
	if (!strcmp(name, "name")) strcpy(sq_conf->name, val);
	if (!strcmp(name, "server")) strcpy(sq_conf->server, val);
	if (!strcmp(name, "mac"))  {
		unsigned mac[6];
		int i;
		// seems to be a Windows scanf buf, cannot support %hhx
		sscanf(val,"%2x:%2x:%2x:%2x:%2x:%2x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		for (i = 0; i < 6; i++) sq_conf->mac[i] = mac[i];
	}
#if defined(RESAMPLE)
	if (!strcmp(name, "resample")) sq_conf->resample = atol(val);
	if (!strcmp(name, "resample_options")) strcpy(sq_conf->resample_options, val);
#endif
	if (!strcmp(name, "enabled")) Conf->Enabled = atol(val);
	if (!strcmp(name, "remove_count"))Conf->RemoveCount = atol(val);
	if (!strcmp(name, "auto_play")) Conf->AutoPlay = atol(val);
	if (!strcmp(name, "idle_timeout")) Conf->IdleTimeout = atol(val);
	if (!strcmp(name, "encryption")) Conf->Encryption = atol(val);
	if (!strcmp(name, "credentials")) strcpy(Conf->Credentials, val);
	if (!strcmp(name, "read_ahead")) Conf->ReadAhead = atol(val);
	if (!strcmp(name, "send_metadata")) Conf->SendMetaData = atol(val);
	if (!strcmp(name, "send_coverart")) Conf->SendCoverArt = atol(val);
	if (!strcmp(name, "friendly_name")) strcpy(Conf->Name, val);
	if (!strcmp(name, "player_volume")) Conf->Volume = atol(val);
	if (!strcmp(name, "volume_mapping")) strcpy(Conf->VolumeMapping, val);
	if (!strcmp(name, "volume_feedback")) Conf->VolumeFeedback = atol(val);
	if (!strcmp(name, "volume_mode")) Conf->VolumeMode = atol(val);
	if (!strcmp(name, "mute_on_pause")) Conf->MuteOnPause = atol(val);
	if (!strcmp(name, "alac_encode")) Conf->AlacEncode = atol(val);
	if (!strcmp(name, "volume_trigger")) Conf->VolumeTrigger = atol(val);
	if (!strcmp(name, "prevent_playback")) strcpy(Conf->PreventPlayback, val);
}

/*----------------------------------------------------------------------------*/
static void LoadGlobalItem(char *name, char *val)
{
	if (!val) return;

	// if (!strcmp(name, "server")) strcpy(glSQServer, val);
	// temporary to ensure parameter transfer from global to common
	if (!strcmp(name, "server")) strcpy(glDeviceParam.server, val);

	if (!strcmp(name, "interface")) strcpy(glInterface, val);
	if (!strcmp(name, "slimproto_log")) slimproto_loglevel = debug2level(val);
	if (!strcmp(name, "stream_log")) stream_loglevel = debug2level(val);
	if (!strcmp(name, "output_log")) output_loglevel = debug2level(val);
	if (!strcmp(name, "decode_log")) decode_loglevel = debug2level(val);
	if (!strcmp(name, "main_log")) main_loglevel = debug2level(val);
	if (!strcmp(name, "slimmain_log")) slimmain_loglevel = debug2level(val);
	if (!strcmp(name, "raop_log")) raop_loglevel = debug2level(val);
	if (!strcmp(name, "util_log")) util_loglevel = debug2level(val);
	if (!strcmp(name, "scan_interval")) glScanInterval = atol(val);
	if (!strcmp(name, "scan_timeout")) glScanTimeout = atol(val);
	if (!strcmp(name, "log_limit")) glLogLimit = atol(val);
 }


/*----------------------------------------------------------------------------*/
void *FindMRConfig(void *ref, char *UDN)
{
	IXML_Element *elm;
	IXML_Node	*device = NULL;
	IXML_NodeList *l1_node_list;
	IXML_Document *doc = (IXML_Document*) ref;
	char *v;
	unsigned i;

	elm = ixmlDocument_getElementById(doc, "squeeze2raop");
	l1_node_list = ixmlDocument_getElementsByTagName((IXML_Document*) elm, "udn");
	for (i = 0; i < ixmlNodeList_length(l1_node_list); i++) {
		IXML_Node *l1_node, *l1_1_node;
		l1_node = ixmlNodeList_item(l1_node_list, i);
		l1_1_node = ixmlNode_getFirstChild(l1_node);
		v = (char*) ixmlNode_getNodeValue(l1_1_node);
		if (v && !strcmp(v, UDN)) {
			device = ixmlNode_getParentNode(l1_node);
			break;
		}
	}
	if (l1_node_list) ixmlNodeList_free(l1_node_list);
	return device;
}

/*----------------------------------------------------------------------------*/
void *LoadMRConfig(void *ref, char *UDN, tMRConfig *Conf, sq_dev_param_t *sq_conf)
{
	IXML_NodeList *node_list;
	IXML_Document *doc = (IXML_Document*) ref;
	IXML_Node *node;
	char *n, *v;
	unsigned i;

	node = (IXML_Node*) FindMRConfig(doc, UDN);
	if (node) {
		node_list = ixmlNode_getChildNodes(node);
		for (i = 0; i < ixmlNodeList_length(node_list); i++) {
			IXML_Node *l1_node, *l1_1_node;
			l1_node = ixmlNodeList_item(node_list, i);
			n = (char*) ixmlNode_getNodeName(l1_node);
			l1_1_node = ixmlNode_getFirstChild(l1_node);
			v = (char*) ixmlNode_getNodeValue(l1_1_node);
			LoadConfigItem(Conf, sq_conf, n, v);
		}
		if (node_list) ixmlNodeList_free(node_list);
	}

	return node;
}

/*----------------------------------------------------------------------------*/
void *LoadConfig(char *name, tMRConfig *Conf, sq_dev_param_t *sq_conf)
{
	IXML_Element *elm;
	IXML_Document	*doc;

	doc = ixmlLoadDocument(name);
	if (!doc) return NULL;

	elm = ixmlDocument_getElementById(doc, "squeeze2raop");
	if (elm) {
		unsigned i;
		char *n, *v;
		IXML_NodeList *l1_node_list;
		l1_node_list = ixmlNode_getChildNodes((IXML_Node*) elm);
		for (i = 0; i < ixmlNodeList_length(l1_node_list); i++) {
			IXML_Node *l1_node, *l1_1_node;
			l1_node = ixmlNodeList_item(l1_node_list, i);
			n = (char*) ixmlNode_getNodeName(l1_node);
			l1_1_node = ixmlNode_getFirstChild(l1_node);
			v = (char*) ixmlNode_getNodeValue(l1_1_node);
			LoadGlobalItem(n, v);
		}
		if (l1_node_list) ixmlNodeList_free(l1_node_list);
	}

	elm = ixmlDocument_getElementById((IXML_Document	*)elm, "common");
	if (elm) {
		char *n, *v;
		IXML_NodeList *l1_node_list;
		unsigned i;
		l1_node_list = ixmlNode_getChildNodes((IXML_Node*) elm);
		for (i = 0; i < ixmlNodeList_length(l1_node_list); i++) {
			IXML_Node *l1_node, *l1_1_node;
			l1_node = ixmlNodeList_item(l1_node_list, i);
			n = (char*) ixmlNode_getNodeName(l1_node);
			l1_1_node = ixmlNode_getFirstChild(l1_node);
			v = (char*) ixmlNode_getNodeValue(l1_1_node);
			LoadConfigItem(&glMRConfig, &glDeviceParam, n, v);
		}
		if (l1_node_list) ixmlNodeList_free(l1_node_list);
	}

	return doc;
 }



