/*
    Author:     Amine Ismail
    email: Amine.Ismail@gmail.com
    
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include "dre.h"
#include "crc64.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

int main (int argc, char ** argv) {
  int fd_cache_idx;
  cache_idx_hdr_t * cache_idx_info;
  if (argc != 2) {
    printf ("Usage: %s cache_idx_file\n", argv[0]);
    exit (-1);
  }
  fd_cache_idx = open(argv[1], O_RDONLY, 0);
  if (fd_cache_idx < 0) {
    perror ("Can't open file: ");
    exit (-1);
  }
  cache_idx_info = (cache_idx_hdr_t *)  
    mmap((caddr_t)0,
	 sizeof(cache_idx_hdr_t), 
	 PROT_READ, 
	 MAP_PRIVATE, fd_cache_idx,
	 0);    
  if (cache_idx_info == (void*)(-1)) {
    perror ("Invalid cache file index");
    exit (-1);
  }
  printf ("%s:\n", argv[1]);
  printf ("\tCache size= %"PRIu64"\n", 
	  cache_idx_info->cache_size);
  printf ("\tHead= %"PRIu32"\n", cache_idx_info->cache_idx_head);
  printf ("\tTail= %"PRIu32"\n", cache_idx_info->cache_idx_tail);
  printf ("\t#block= %"PRIu64"\n", cache_idx_info->nb_block);
  printf ("\t#block since start= %"PRIu64"\n", cache_idx_info->nb_total_block);
  printf ("\tData write pos= %"PRIu64"\n", cache_idx_info->data_pos_wr);
  printf ("\tLast blk size= %"PRIu32"\n", 
	  cache_idx_info->last_block_size);
  
  printf ("\n");
  return 0;
}
