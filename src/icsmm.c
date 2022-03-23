#include "icsmm.h"
#include "debug.h"
#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>


/*
 * The allocator MUST store the head of its free list in this variable. 
 * Doing so will make it accessible via the extern keyword.
 * This will allow ics_freelist_print to access the value from a different file.
 */
ics_free_header *freelist_head = NULL;
void* heap_start = NULL;

ics_free_header* insert_free_list(
    ics_free_header* free_header,
    ics_free_header* node) {
  ics_free_header* curr = free_header;
  ics_free_header* prev = NULL;
  while (curr) {
    size_t size = GET_BSIZE(&curr->header);
    if (size >= GET_BSIZE(&node->header)) {
      break;
    }
    prev = curr;
    curr = curr->next;
  }
  if (prev == NULL) {
    free_header = node;
  } else {
    prev->next = node;
  }
  node->next = curr;

  if (curr) {
    curr->prev = node;
  }
  node->prev = prev;
  return free_header;
}

ics_free_header* remove_free_list(
    ics_free_header* free_header,
    ics_free_header* node) {
  ics_free_header* prev = node->prev;
  if (prev == NULL) {
    free_header = node->next;
  } else {
    prev->next = node->next;
  }

  if (node->next) {
    node->next->prev = prev;
  }
  return free_header;
}

ics_free_header* ics_mm_init() {
  void *ptr = ics_inc_brk();
  if (ptr == (void*)-1) {
    errno = ENOMEM;
    return NULL;
  }

  // Prologue header
  set_ics_header(ptr, 0, 0, 1);

  void *bp = ptr + 2 * HEADERSIZE;
  set_ics_header(get_hdrp(bp), 0, PAGE_SIZE - 2 * HEADERSIZE, 0);
  set_ics_footer(get_ftrp(bp), 0, PAGE_SIZE - 2 * HEADERSIZE, 0);
  ics_free_header* head = ptr + HEADERSIZE;
  head->next = NULL;
  head->prev = NULL;

  // Epilogue footer
  set_ics_footer(get_ftrp(bp) + HEADERSIZE, 0, 0, 1);
  heap_start = ptr;
  return head;
}

void* coalesce(void *bp) {
  ics_header *curr = get_hdrp(bp);
  ics_header *prev = get_hdrp(prev_blkp(bp));
  if (bp - 2 * HEADERSIZE == heap_start) {
    prev = (ics_header*)heap_start;
  }

  ics_header *next = get_hdrp(next_blkp(bp));
  bool prev_alloc = GET_ALLOC(prev);
  bool next_alloc = GET_ALLOC(next);

  if (prev_alloc && next_alloc) {
    return bp;
  } 

  if (!prev_alloc) {
    freelist_head = remove_free_list(freelist_head, (ics_free_header*)prev);
  }
  if (!next_alloc) {
    freelist_head = remove_free_list(freelist_head, (ics_free_header*)next);
  }

  if (prev_alloc && !next_alloc) {
    curr->block_size += next->block_size;
    set_ics_footer(get_ftrp(bp), 0, curr->block_size, 0);
  } else if (!prev_alloc && next_alloc) {
    prev->block_size += curr->block_size;
    set_ics_footer(get_ftrp(bp), 0, prev->block_size, 0);
    bp = prev_blkp(bp);
  } else {
    prev->block_size += curr->block_size;
    prev->block_size += next->block_size;
    set_ics_footer(get_ftrp(next_blkp(bp)), 0, prev->block_size, 0);
    bp = prev_blkp(bp);
  }
  return bp;
}

void* extend_heap() {
  void *ptr = ics_inc_brk();
  if (ptr == (void*)-1) {
    errno = ENOMEM;
    return NULL;
  }
  set_ics_header(get_hdrp(ptr), 0, PAGE_SIZE, 0);
  set_ics_header(get_ftrp(ptr), 0, PAGE_SIZE, 0);
  set_ics_footer(get_hdrp(next_blkp(ptr)), 0, 0, 1);
  ptr = coalesce(ptr);
  freelist_head = insert_free_list(freelist_head, get_hdrp(ptr));
  return ptr;
}

void* find_first_fit(ics_free_header* free_header, size_t size) {
  while (free_header) {
    if (GET_BSIZE(&free_header->header) >= size) {
      return free_header;
    }
    free_header = free_header->next;
  }
  return NULL;
}

/*
 * This is your implementation of malloc. It acquires uninitialized memory from  
 * ics_inc_brk() that is 16-byte aligned, as needed.
 *
 * @param size The number of bytes requested to be allocated.
 *
 * @return If successful, the pointer to a valid region of memory of at least the
 * requested size is returned. Otherwise, NULL is returned and errno is set to 
 * ENOMEM - representing failure to allocate space for the request.
 * 
 * If size is 0, then NULL is returned and errno is set to EINVAL - representing
 * an invalid request.
 */
void *ics_malloc(size_t size) { 
  static bool first = true;
  if (first) {
    freelist_head = ics_mm_init();
    if (freelist_head == NULL) {
      return NULL;
    }
    first = false;
  }

  if (size == 0){
    errno = EINVAL;
    return NULL;
  }

  size_t requested_size = size;
  size += 2 * HEADERSIZE;
  if (size < MIN_CHUNK_SIZE) {
    size = MIN_CHUNK_SIZE;
  }
  size_t left = size % ALGIN_SIZE;
  if (left > 0) {
    size += (ALGIN_SIZE - left);
  }
  if (size >= 5 * PAGE_SIZE){
    errno = ENOMEM;
    return NULL;
  }

  ics_free_header* found = find_first_fit(freelist_head, size);
  while (found == NULL) {
    if (extend_heap() == NULL) {
      return NULL;
    }
    found = find_first_fit(freelist_head, size);
  }

  freelist_head = remove_free_list(freelist_head, found);
  size_t newblock_size = GET_BSIZE(&found->header);
  if (GET_BSIZE(&found->header) >= size + MIN_CHUNK_SIZE) {
    newblock_size = size;
    void* nptr = (void*)found + size;
    size_t left_size = GET_BSIZE(&found->header) - size;
    set_ics_header(nptr, 0, left_size, 0);
    set_ics_footer(get_ftrp(nptr+ HEADERSIZE), 0, left_size, 0);
    freelist_head = insert_free_list(freelist_head, nptr);
  }

  set_ics_header(found, requested_size, newblock_size, 1);
  set_ics_footer(get_ftrp((void*)found + HEADERSIZE), requested_size, newblock_size, 1);

  ics_freelist_print_compact();

  return (void*)found + HEADERSIZE;
}

static int check_ptr(void* ptr) {
  void* heap_end = ics_get_brk() - HEADERSIZE;
  if (ptr < heap_start || ptr >= heap_end) {
    return -1;
  }
  ics_header *header = get_hdrp(ptr);
  if (header->hid != HEADER_MAGIC || !GET_ALLOC(header)) {
    return -1;
  }
  ics_footer *footer = get_ftrp(ptr);
  if (footer->fid != FOOTER_MAGIC || !GET_ALLOC(footer)) {
    return -1;
  }
  if (GET_BSIZE(header) != GET_BSIZE(footer) ||
      header->requested_size != footer->requested_size) {
    return -1;
  }
  return 0;
}

/*
 * Marks a dynamically allocated block as no longer in use and coalesces with 
 * adjacent free blocks (as specified by Homework Document). 
 * Adds the block to the appropriate bucket according to the block placement policy.
 *
 * @param ptr Address of dynamically allocated memory returned by the function
 * ics_malloc.
 * 
 * @return 0 upon success, -1 if error and set errno accordingly.
 */
int ics_free(void *ptr) { 
  if (check_ptr(ptr) == -1) {
    errno = EINVAL;
    return -1;
  }

  ics_header *header = get_hdrp(ptr);
  ics_footer *footer = get_ftrp(ptr);
  size_t size = GET_BSIZE(header);
  set_ics_header(header, 0, size, 0);
  set_ics_footer(footer, 0, size, 0);
  ptr = coalesce(ptr);
  freelist_head = insert_free_list(freelist_head, get_hdrp(ptr));
  ics_freelist_print_compact();
  return 0;
}

/********************** EXTRA CREDIT ***************************************/

/*
 * Resizes the dynamically allocated memory, pointed to by ptr, to at least size 
 * bytes. See Homework Document for specific description.
 *
 * @param ptr Address of the previously allocated memory region.
 * @param size The minimum size to resize the allocated memory to.
 * @return If successful, the pointer to the block of allocated memory is
 * returned. Else, NULL is returned and errno is set appropriately.
 *
 * If there is no memory available ics_malloc will set errno to ENOMEM. 
 *
 * If ics_realloc is called with an invalid pointer, set errno to EINVAL. See ics_free
 * for more details.
 *
 * If ics_realloc is called with a valid pointer and a size of 0, the allocated     
 * block is free'd and return NULL.
 */
void *ics_realloc(void *ptr, size_t size) {
  if(check_ptr(ptr) == -1) {
    errno = EINVAL;
    return NULL;
  }

  if (size == 0) {
    ics_free(ptr);
    return NULL;
  }
  void *new_ptr = ics_malloc(size);
  if (new_ptr == NULL) {
    return NULL;
  }
  ics_header* oldheader = get_hdrp(ptr);
  size_t copysize = 
    size < oldheader->requested_size ? size : oldheader->requested_size;
  memcpy(new_ptr, ptr, copysize);
  ics_free(ptr);
  return new_ptr;
}
