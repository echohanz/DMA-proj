#ifndef HELPERS_H
#define HELPERS_H

#include "icsmm.h"
#include <stdbool.h>

#define HEADERSIZE      8
#define MIN_CHUNK_SIZE  32
#define ALGIN_SIZE      16
#define PAGE_SIZE       4096

#define GET_BSIZE(hd) (((hd)->block_size) & ~0x7)
#define GET_ALLOC(hd) (((hd)->block_size) & 0x1)

void *get_hdrp(void *p);
void *get_ftrp(void *p);

void *next_blkp(void *p);
void *prev_blkp(void *p);

void set_ics_header(void *p, uint64_t requested_size, uint64_t block_size, bool alloced);
void set_ics_footer(void *p, uint64_t requested_size, uint64_t block_size, bool alloced);
#endif
