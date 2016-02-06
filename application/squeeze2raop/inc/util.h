/*
 *  Squeeze2upnp - LMS to uPNP gateway
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

#ifndef __UTIL_H
#define __UTIL_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pthread.h"
#include "ixml.h"
#include "platform.h"

typedef struct sQueue {
	struct sQueue *next;
	void *item;
} tQueue;

void		*GetRequest(tQueue *Queue, pthread_mutex_t *Mutex, pthread_cond_t *Cond, u32_t timeout);
void 		QueueInit(tQueue *queue);
void 		QueueInsert(tQueue *queue, void *item);
void 		*QueueExtract(tQueue *queue);
void 		QueueFlush(tQueue *queue);
int			pthread_cond_reltimedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, u32_t msWait);

char 		*XMLGetFirstDocumentItem(IXML_Document *doc, const char *item);
char 		*XMLGetFirstElementItem(IXML_Element *element, const char *item);
IXML_Node   *XMLAddNode(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...);
int 		XMLAddAttribute(IXML_Document *doc, IXML_Node *parent, char *name, char *fmt, ...);

#endif
