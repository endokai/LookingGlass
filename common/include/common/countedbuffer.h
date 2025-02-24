/**
 * Looking Glass
 * Copyright (C) 2017-2021 The Looking Glass Authors
 * https://looking-glass.io
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _H_LG_COMMON_COUNTEDBUFFER_
#define _H_LG_COMMON_COUNTEDBUFFER_

#include <stddef.h>

struct CountedBuffer {
  _Atomic(size_t) refs;
  size_t size;
  char data[];
};

struct CountedBuffer * countedBufferNew(size_t size);
void countedBufferAddRef(struct CountedBuffer * buffer);
void countedBufferRelease(struct CountedBuffer ** buffer);

#endif
