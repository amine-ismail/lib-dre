/*
    Author:     Amine Ismail
    email: Amine.Ismail@gmail.com
    
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include "dre.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#ifdef TEST_SMALL_BUFF
#define READ_SZ 12
#else
#define READ_SZ 4096
#endif

/*--------------------------------------------------------------------*/

int main (int argc, char ** argv) {

  int fd =0;

#ifndef TEST_PERF
  int fd_out=0;
#endif

  int sz=0;
  uint8_t buff [READ_SZ];
  c_buff_t * out, *tmp;
  dre_stream_t * str_ctx;
  uint8_t avg_blk_sz_bits = 8;
  uint8_t win_sz;
  uint16_t rsz;
#ifndef TEST_PERF
  int nb_tblocks=0;
#endif
  dre_cache_t * cache;
  uint32_t min_blk_sz, max_blk_sz;

#ifndef TEST_PERF
#define OUT_FILE_NAME_LEN 64
  char outFile [OUT_FILE_NAME_LEN];
#endif

#ifdef TEST_LOAD_CACHE
  uint64_t nb_blocks;
#endif

#if defined(TEST_EDGE)
  uint64_t cache_sz=1024*1024*11;
#elif defined(TEST_EDGE1)
  uint64_t cache_sz=1024*1024*50;
#elif defined(TEST_EDGE1)
  uint64_t cache_sz=1024*1024*200;
#else
  uint64_t cache_sz=1024*1024*500;
#endif

  
  if (argc <6) {
    printf ("%s window_size ln2(avgblk_size) min_blk_sz(bytes) max_blk_sz(bytes)file\n", argv[0]);
    exit (0);
  }
  
  win_sz = atoi(argv[1]);
  avg_blk_sz_bits=atoi(argv[2]);
  min_blk_sz=atol(argv[3]);
  max_blk_sz=atol(argv[4]);
  /* init  DRE */
  init_rabin_lookup_table ();
  /* create a cache */
#ifdef TEST_LOAD_CACHE
  cache = comp_load_dre_cache ("./comp.db", cache_sz, &nb_blocks, 0, 16, 0, 0,64);
  if (!cache) {
    return -1;
  }
#else
  cache = new_dre_cache ("./comp.db",cache_sz,16);
#endif

  if (!cache) {
    printf ("Can't create cache object\n");
    exit(1);
  }
  /* create a stream context */
  str_ctx = new_dre_stream_ctx (cache,avg_blk_sz_bits,
				min_blk_sz,
				max_blk_sz,
				win_sz);

  fd = open(argv[5], O_RDONLY);
#ifndef TEST_PERF
  snprintf(outFile, OUT_FILE_NAME_LEN,"%s.comp",argv[5]);
  fd_out = open(outFile, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU);
#endif

  if (!fd) {
    printf ("can't open %s for reading\n", argv[1]);
    return -1;
  }
  rsz=READ_SZ;
  while (1) {
#ifdef TEST_RND_RD_SZ
#define MAX_RD_SZ 1500
#define MIN_RD_SZ 16
    rsz=(random()%MAX_RD_SZ) + MIN_RD_SZ;
#endif
    sz=read(fd, buff, rsz);
    if (sz==0) {
      break;
    }
#ifdef TEST_PERF
    {
      uint64_t bitrate;
      uint64_t delta_byte=0;
      uint64_t delta_time=0;
      static struct timeval ptime, ctime;
      static uint64_t process_sz=0, prev_sz=0; 
      static uint64_t count=0;
      count++;
      process_sz+=sz;
      if (count%1000==0) {
	gettimeofday(&ctime, NULL);
	delta_time=((ctime.tv_sec - ptime.tv_sec)*1000000) + 
	  (ctime.tv_usec - ptime.tv_usec);
	ptime=ctime;
	delta_byte=process_sz-prev_sz;
	prev_sz=process_sz;
	bitrate=(delta_byte*8*1000000)/delta_time;
	printf ("Bitrate => %" PRIu64 " bits/sec\n", bitrate);
      }
    }
#endif

#ifdef TEST_DESYNCHRO
    if (nb_tblocks == 3100) {
    uint64_t nb;
      close_cache(cache);
      cache = comp_load_dre_cache ("./comp.db", cache_sz, &nb, 300, 16, 0, 0,64);
      str_ctx->cache=cache;
    }
#endif
    out = rabin_compress_buffer (str_ctx, buff, sz);
    
    while (out) {
#ifndef TEST_PERF
      if (out->compressed==0) {
	nb_tblocks++;
#ifdef TEST_DESYNCHRO
	if ((nb_tblocks<300) || (nb_tblocks>310)) {
#endif
	write (fd_out, &out->compressed, 1);
	  write (fd_out, &(out->blk_len), sizeof(out->blk_len));
	  write (fd_out, out->data, out->blk_len);
#ifdef TEST_DESYNCHRO
	}
#endif
      } else {
	write (fd_out, &out->compressed, 1);
	write (fd_out, &(out->dict_offset), sizeof(out->dict_offset));
	write (fd_out, &(out->blk_len), sizeof(out->blk_len));
      }
#endif /*TEST_PERF*/
      tmp=out;
      out=out->nxt;
      free(tmp);
    }

#ifdef TEST_RND_FLUSH
#define FLUSH_FREQ 10
    {
      int r = random()%FLUSH_FREQ;
      if (r==0) {
	out=rabin_flush(str_ctx);
	if (out) {
	  write (fd_out, &out->compressed, 1);
	  if (out->compressed==0) {
	    write (fd_out, &(out->blk_len), sizeof(out->blk_len));
	    write (fd_out, out->data, out->blk_len);
	  } else {
	    write (fd_out, &(out->dict_offset), sizeof(out->dict_offset));
	    write (fd_out, &(out->blk_len), sizeof(out->blk_len));
	  }
	  free(out);
	}
      }
    }
#endif 
  }

#ifndef TEST_PERF
  out=rabin_flush(str_ctx);
  if (out) {
    write (fd_out, &out->compressed, 1);
    if (out->compressed==0) {
      write (fd_out, &(out->blk_len), sizeof(out->blk_len));
      write (fd_out, out->data, out->blk_len);
    } else {
      write (fd_out, &(out->dict_offset), sizeof(out->dict_offset));
      write (fd_out, &(out->blk_len), sizeof(out->blk_len));
    }
    free(out);
  }
  close(fd_out);
#endif
  free_dre_ctx(str_ctx);
  close_cache(cache);
  return 0;
}
