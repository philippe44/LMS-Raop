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
//static log_level 	*loglevel = &util_loglevel;

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
		root = ixmlNode_cloneNode((IXML_Node*) old_root, true);
		ixmlNode_appendChild((IXML_Node*) doc, root);

		list = ixmlDocument_getElementsByTagName((IXML_Document*) root, "device");
		for (i = 0; i < (int) ixmlNodeList_length(list); i++) {
			IXML_Node *device;

			device = ixmlNodeList_item(list, i);
			ixmlNode_removeChild(root, device, &device);
			ixmlNode_free(device);
		}
		if (list) ixmlNodeList_free(list);
	}
	else {
		root = XMLAddNode(doc, NULL, "squeeze2raop", NULL);


		// XMLAddNode(doc, root, "server", glSQServer);
		XMLAddNode(doc, root, "interface", glInterface);
		XMLAddNode(doc, root, "slimproto_log", level2debug(slimproto_loglevel));
		XMLAddNode(doc, root, "stream_log", level2debug(stream_loglevel));
		XMLAddNode(doc, root, "output_log", level2debug(output_loglevel));
		XMLAddNode(doc, root, "decode_log", level2debug(decode_loglevel));
		XMLAddNode(doc, root, "main_log",level2debug(main_loglevel));
		XMLAddNode(doc, root, "slimmain_log", level2debug(slimmain_loglevel));
		XMLAddNode(doc, root, "raop_log",level2debug(raop_loglevel));
		XMLAddNode(doc, root, "util_log",level2debug(util_loglevel));
		XMLAddNode(doc, root, "scan_interval", "%d", (u32_t) glScanInterval);
		XMLAddNode(doc, root, "scan_timeout", "%d", (u32_t) glScanTimeout);
		XMLAddNode(doc, root, "log_limit", "%d", (s32_t) glLogLimit);

		common = XMLAddNode(doc, root, "common", NULL);
		XMLAddNode(doc, common, "streambuf_size", "%d", (u32_t) glDeviceParam.stream_buf_size);
		XMLAddNode(doc, common, "output_size", "%d", (u32_t) glDeviceParam.output_buf_size);
		XMLAddNode(doc, common, "enabled", "%d", (int) glMRConfig.Enabled);
		XMLAddNode(doc, common, "codecs", glDeviceParam.codecs);
		XMLAddNode(doc, common, "sample_rate", "%d", (int) glDeviceParam.sample_rate);
#if defined(RESAMPLE)
		XMLAddNode(doc, common, "resample", "%d", (int) glDeviceParam.resample);
		XMLAddNode(doc, common, "resample_options", glDeviceParam.resample_options);
#endif
		XMLAddNode(doc, common, "player_volume", "%d", (int) glDeviceParam.player_volume);
		XMLAddNode(doc, common, "send_metadata", "%d", (int) glMRConfig.SendMetaData);
		XMLAddNode(doc, common, "send_coverart", "%d", (int) glMRConfig.SendCoverArt);
		XMLAddNode(doc, common, "remove_count", "%d", (u32_t) glMRConfig.RemoveCount);
		XMLAddNode(doc, common, "auto_play", "%d", (int) glMRConfig.AutoPlay);
		XMLAddNode(doc, common, "idle_timeout", "%d", (int) glMRConfig.IdleTimeout);

		XMLAddNode(doc, common, "encryption", "%d", (int) glMRConfig.Encryption);
		XMLAddNode(doc, common, "read_ahead", "%d", (int) glDeviceParam.read_ahead);
		XMLAddNode(doc, common, "server", glDeviceParam.server);
	}

	for (i = 0; i < MAX_RENDERERS; i++) {
		IXML_Node *dev_node;

		if (!glMRDevices[i].InUse) continue;
		else p = &glMRDevices[i];

		if (old_doc && ((dev_node = (IXML_Node*) FindMRConfig(old_doc, p->UDN)) != NULL)) {
			dev_node = ixmlNode_cloneNode(dev_node, true);
			ixmlNode_appendChild((IXML_Node*) doc, root);
		}
		else {
			dev_node = XMLAddNode(doc, root, "device", NULL);
			XMLAddNode(doc, dev_node, "udn", p->UDN);
			XMLAddNode(doc, dev_node, "name", *(p->Config.Name) ? p->Config.Name : p->FriendlyName);
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
			device = ixmlNode_cloneNode(device, true);
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
	if (!strcmp(name, "player_volume")) sq_conf->player_volume = atol(val);
	if (!strcmp(name, "read_ahead")) sq_conf->read_ahead = atol(val);
	if (!strcmp(name, "enabled")) Conf->Enabled = atol(val);
	if (!strcmp(name, "codecs")) strcpy(sq_conf->codecs, val);
	if (!strcmp(name, "sample_rate")) sq_conf->sample_rate = atol(val);
#if defined(RESAMPLE)
	if (!strcmp(name, "resample")) sq_conf->resample = atol(val);
	if (!strcmp(name, "resample_options")) strcpy(sq_conf->resample_options, val);
#endif
	if (!strcmp(name, "remove_count"))Conf->RemoveCount = atol(val);
	if (!strcmp(name, "player_volume")) sq_conf->player_volume = atol(val);
	if (!strcmp(name, "auto_play")) Conf->AutoPlay = atol(val);
	if (!strcmp(name, "idle_timeout")) Conf->IdleTimeout = atol(val);
	if (!strcmp(name, "encryption")) Conf->Encryption = atol(val);
	if (!strcmp(name, "read_ahead")) sq_conf->read_ahead = atol(val);
	if (!strcmp(name, "send_metadata")) Conf->SendMetaData = atol(val);
	if (!strcmp(name, "send_coverart")) Conf->SendCoverArt = atol(val);
	if (!strcmp(name, "name")) strcpy(Conf->Name, val);
	if (!strcmp(name, "server")) strcpy(sq_conf->server, val);
	if (!strcmp(name, "mac"))  {
		unsigned mac[6];
		int i;
		// seems to be a Windows scanf buf, cannot support %hhx
		sscanf(val,"%2x:%2x:%2x:%2x:%2x:%2x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
		for (i = 0; i < 6; i++) sq_conf->mac[i] = mac[i];
	}
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



