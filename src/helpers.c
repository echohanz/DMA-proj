#include "helpers.h"
#include "debug.h"
#include "icsmm.h"

/* Helper function definitions go here */

void *get_hdrp(void *p) {
  return p - HEADERSIZE;
}

void *get_ftrp(void *p) {
  ics_header *header = get_hdrp(p);
  return p + GET_BSIZE(header) - 2 * HEADERSIZE;
}

void *next_blkp(void *p) {
  ics_header *header = get_hdrp(p);
  return p + GET_BSIZE(header);
}

void *prev_blkp(void *p) {
  ics_footer *header = p - 2 * HEADERSIZE;
  return p - GET_BSIZE(header);
}

void set_ics_header(void *p, uint64_t requested_size, uint64_t block_size, bool alloced) {
  ics_header *header = p;
  header->requested_size = requested_size;
  header->hid = HEADER_MAGIC;
  header->block_size = block_size;
  if (alloced) {
    header->block_size |= 1;
  }
}

void set_ics_footer(void *p, uint64_t requested_size, uint64_t block_size, bool alloced) {
  ics_footer *header = p;
  header->requested_size = requested_size;
  header->fid = FOOTER_MAGIC;
  header->block_size = block_size;
  if (alloced) {
    header->block_size |= 1;
  }
}

