/*****************************************************************************
 * raop_client.h: RAOP Client
 *
 * Copyright (C) 2004 Shiro Ninomiya <shiron@snino.com>
 * Copyright (C) 2016 Philippe <philippe44@outlook.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/
 
#ifndef __RAOP_PLAY_H_
#define __RAOP_PLAY_H_

#define	RAOP_FD_READ (1<<0)
#define RAOP_FD_WRITE (1<<1)

#define RAOP_CONNECTED "connected"
#define RAOP_SONGDONE "done"
#define RAOP_ERROR "error"

typedef int (*fd_callback_t)(void *, int);
int set_fd_event(int fd, int flags, fd_callback_t cbf, void *p);
int clear_fd_event(int fd);

#define SERVER_PORT 5000

#define MAX_NUM_OF_FDS 4

typedef struct fdev_t{
        int fd;
        void *dp;
        fd_callback_t cbf;
        int flags;
}dfev_t;

typedef struct raopld_t{
        raopcl_t *raopcl;
        struct auds_s *auds;
        dfev_t fds[MAX_NUM_OF_FDS];
}raopld_t;

#endif
