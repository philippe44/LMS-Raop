/*****************************************************************************
 * audio_stream.c: audio file stream
 *
 * Copyright (C) 2005 Shiro Ninomiya <shiron@snino.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "alac_wrapper.h"


/*----------------------------------------------------------------------------*/
static inline void bits_write(__u8 **p, __u8 d, int blen, int *bpos)
{
	int lb,rb,bd;
	lb=7-*bpos;
	rb=lb-blen+1;
	if(rb>=0){
		bd=d<<rb;
		if(*bpos)
			**p|=bd;
		else
			**p=bd;
		*bpos+=blen;
	}else{
		bd=d>>-rb;
		**p|=bd;
		*p+=1;
		**p=d<<(8+rb);
		*bpos=-rb;
	}
}


/*----------------------------------------------------------------------------*/
bool pcm_to_alac(u8_t *in, int in_size, u8_t **out, int *size, int bsize, int channels, bool big_endian)
{
	int bpos = 0;
	__u8 *bp;
	int i;

	*out = malloc(bsize * 4 + 16);
	bp = *out;

	bits_write(&bp, 1, 3, &bpos); // channel=1, stereo
	bits_write(&bp, 0, 4, &bpos); // unknown
	bits_write(&bp, 0, 12, &bpos); // unknown
	bits_write(&bp, 1, 1, &bpos); // has-size
	bits_write(&bp, 0, 2, &bpos); // unused
	bits_write(&bp, 1, 1, &bpos); // is-not-compressed
	bits_write(&bp,(bsize>>24) & 0xff, 8, &bpos); // size of data, integer, big endian
	bits_write(&bp,(bsize>>16) & 0xff, 8, &bpos);
	bits_write(&bp,(bsize>>8) & 0xff, 8, &bpos);
	bits_write(&bp, bsize & 0xff, 8, &bpos);

	if (channels == 1) {
		if (big_endian)
			for (i = 0; i < in_size; i++) {
				bits_write(&bp, in[i*2], 8, &bpos);
				bits_write(&bp, in[i*2+1], 8, &bpos);
				bits_write(&bp, in[i*2], 8, &bpos);
				bits_write(&bp, in[i*2+1], 8, &bpos);
			}
		else
			for (i = 0; i < in_size; i++) {
				bits_write(&bp, in[i*2+1], 8, &bpos);
				bits_write(&bp, in[i*2], 8, &bpos);
				bits_write(&bp, in[i*2+1], 8, &bpos);
				bits_write(&bp, in[i*2], 8, &bpos);
			}
	}
	else {
		if (big_endian)
			for (i = 0; i < in_size; i++) {
				bits_write(&bp, in[i*4],8,&bpos);
				bits_write(&bp, in[i*4+1],8,&bpos);
				bits_write(&bp, in[i*4+2],8,&bpos);
				bits_write(&bp, in[i*4+3],8,&bpos);
			}
		else
			for (i = 0; i < in_size; i++) {
				bits_write(&bp, in[i*4+1],8,&bpos);
				bits_write(&bp, in[i*4],8,&bpos);
				bits_write(&bp, in[i*4+3],8,&bpos);
				bits_write(&bp, in[i*4+2],8,&bpos);
			}
	}

	// when readable size is less than bsize, fill 0 at the bottom
	for(i = 0; i < (bsize - in_size) * 4; i++) {
		bits_write(&bp, 0, 8, &bpos);
	}

	// frame footer ??
	bits_write(&bp, 7, 3, &bpos); // should be always 7 ( says wikipedia )

	*size = bp - *out;
	if (bpos) *size +=1;

	return true;
}


