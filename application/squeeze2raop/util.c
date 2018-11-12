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

#include "ixml.h"
#include "util.h"
#include "util_common.h"
#include "log_util.h"


/*----------------------------------------------------------------------------*/
/* locals */
/*----------------------------------------------------------------------------*/
extern log_level	util_loglevel;
static log_level 	*loglevel = &util_loglevel;

#if WIN
/*----------------------------------------------------------------------------*/
void winsock_init(void) {
	WSADATA wsaData;
	WORD wVersionRequested = MAKEWORD(2, 2);
	int WSerr = WSAStartup(wVersionRequested, &wsaData);
	if (WSerr != 0) {
		LOG_ERROR("Bad winsock version", NULL);
		exit(1);
	}
}


/*----------------------------------------------------------------------------*/
void winsock_close(void) {

	WSACleanup();
}
#endif


/*----------------------------------------------------------------------------*/
void close_socket(int sd) {
#if WIN
		shutdown(sd, SD_BOTH);
#else
		shutdown(sd, SHUT_RDWR);
#endif
		closesocket(sd);
}


/*----------------------------------------------------------------------------*/
int pthread_cond_reltimedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, u32_t msWait)
{
	struct timespec ts;
	u32_t	nsec;
#if OSX
	struct timeval tv;
#endif

#if WIN
	struct _timeb SysTime;

	_ftime(&SysTime);
	ts.tv_sec = (long) SysTime.time;
	ts.tv_nsec = 1000000 * SysTime.millitm;
#elif LINUX || FREEBSD || SUNOS
	clock_gettime(CLOCK_REALTIME, &ts);
#elif OSX
	gettimeofday(&tv, NULL);
	ts.tv_sec = (long) tv.tv_sec;
	ts.tv_nsec = 1000L * tv.tv_usec;
#endif

	if (!msWait) return pthread_cond_wait(cond, mutex);

	nsec = ts.tv_nsec + (msWait % 1000) * 1000000;
	ts.tv_sec += msWait / 1000 + (nsec / 1000000000);
	ts.tv_nsec = nsec % 1000000000;

	return pthread_cond_timedwait(cond, mutex, &ts);
}


/*----------------------------------------------------------------------------*/
void *GetRequest(tQueue *Queue, pthread_mutex_t *Mutex, pthread_cond_t *Cond, u32_t timeout)
{
	void *data;

	pthread_mutex_lock(Mutex);
	data = QueueExtract(Queue);
	if (!data) pthread_cond_reltimedwait(Cond, Mutex, timeout);
	pthread_mutex_unlock(Mutex);

	return data;
}


/*----------------------------------------------------------------------------*/
void QueueInit(tQueue *queue)
{
	queue->item = NULL;
}


/*----------------------------------------------------------------------------*/
void QueueInsert(tQueue *queue, void *item)
{
	while (queue->item)	queue = queue->next;
	queue->item = item;
	queue->next = malloc(sizeof(tQueue));
	queue->next->item = NULL;
}


/*----------------------------------------------------------------------------*/
void *QueueExtract(tQueue *queue)
{
	void *item = queue->item;
	tQueue *next = queue->next;

	if (item) {
		queue->item = next->item;
		if (next->item) queue->next = next->next;
		NFREE(next);
	}

	return item;
}

/*----------------------------------------------------------------------------*/
void QueueFlush(tQueue *queue)
{
	void *item = queue->item;
	tQueue *next = queue->next;

	queue->item = NULL;

	while (item) {
		next = queue->next;
		item = next->item;
		if (next->item) queue->next = next->next;
		NFREE(next);
	}
}


/*----------------------------------------------------------------------------*/
char *XMLGetFirstDocumentItem(IXML_Document *doc, const char *item)
{
	IXML_NodeList *nodeList = NULL;
	IXML_Node *textNode = NULL;
	IXML_Node *tmpNode = NULL;
	char *ret = NULL;

	nodeList = ixmlDocument_getElementsByTagName(doc, (char *)item);
	if (nodeList) {
		tmpNode = ixmlNodeList_item(nodeList, 0);
		if (tmpNode) {
			textNode = ixmlNode_getFirstChild(tmpNode);
			if (!textNode) {
				LOG_WARN("(BUG) ixmlNode_getFirstChild(tmpNode) returned NULL", NULL);
				ret = strdup("");
			}
			else {
				ret = strdup(ixmlNode_getNodeValue(textNode));
				if (!ret) {
					LOG_WARN("ixmlNode_getNodeValue returned NULL", NULL);
					ret = strdup("");
				}
			}
		} else
			LOG_WARN("ixmlNodeList_item(nodeList, 0) returned NULL", NULL);
	} else
		LOG_SDEBUG("Error finding %s in XML Node", item);

	if (nodeList) ixmlNodeList_free(nodeList);

	return ret;
}

/*----------------------------------------------------------------------------*/
char *XMLGetFirstElementItem(IXML_Element *element, const char *item)
{
	IXML_NodeList *nodeList = NULL;
	IXML_Node *textNode = NULL;
	IXML_Node *tmpNode = NULL;
	char *ret = NULL;

	nodeList = ixmlElement_getElementsByTagName(element, (char *)item);
	if (nodeList == NULL) {
		LOG_WARN("Error finding %s in XML Node", item);
		return NULL;
	}
	tmpNode = ixmlNodeList_item(nodeList, 0);
	if (!tmpNode) {
		LOG_WARN("Error finding %s value in XML Node", item);
		ixmlNodeList_free(nodeList);
		return NULL;
	}
	textNode = ixmlNode_getFirstChild(tmpNode);
	ret = strdup(ixmlNode_getNodeValue(textNode));
	if (!ret) {
		LOG_ERROR("Error allocating memory for %s in XML Node",item);
		ixmlNodeList_free(nodeList);
		return NULL;
	}
	ixmlNodeList_free(nodeList);

	return ret;
}


/*----------------------------------------------------------------------------*/
IXML_Node *XMLAddNode(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...)
{
	IXML_Node *node, *elm;
//	IXML_Element *elm;

	char buf[256];
	va_list args;

	elm = (IXML_Node*) ixmlDocument_createElement(doc, name);
	if (parent) ixmlNode_appendChild(parent, elm);
	else ixmlNode_appendChild((IXML_Node*) doc, elm);

	if (fmt) {
		va_start(args, fmt);

		vsprintf(buf, fmt, args);
		node = ixmlDocument_createTextNode(doc, buf);
		ixmlNode_appendChild(elm, node);

		va_end(args);
	}

	return elm;
}


/*----------------------------------------------------------------------------*/
IXML_Node *XMLUpdateNode(IXML_Document *doc, IXML_Node *parent, bool refresh, char *name, char *fmt, ...)
{
	char buf[256];
	va_list args;
	IXML_Node *node = (IXML_Node*) ixmlDocument_getElementById((IXML_Document*) parent, name);

	va_start(args, fmt);
	vsprintf(buf, fmt, args);

	if (!node) XMLAddNode(doc, parent, name, buf);
	else if (refresh) {
		node = ixmlNode_getFirstChild(node);
		ixmlNode_setNodeValue(node, buf);
	}

	va_end(args);

	return node;
}


/*----------------------------------------------------------------------------*/
int XMLAddAttribute(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...)
{
	char buf[256];
	int ret;
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, 256, fmt, args);
	ret = ixmlElement_setAttribute((IXML_Element*) parent, name, buf);
	va_end(args);

	return ret;
}




