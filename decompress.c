/*
    Author:     Amine Ismail
    email: Amine.Ismail@gmail.com
    
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include "dre.h"

#define true 1
#define false 0

int main (int argc, char ** argv) {
  int fd =0, fd_out=0;
  uint8_t * buff=NULL;
  uint32_t buff_len=0;
  int sz=0;

#ifdef TEST_EDGE
  uint64_t cache_sz=1024*1024*11;
#else
  uint64_t cache_sz=1024*1024*500;
#endif
  
  dre_cache_t * cache;
#define OUT_FILE_NAME_LEN 64
  char outFile [OUT_FILE_NAME_LEN];
  dre_stream_t * str_ctx;
  uint64_t offset;
  uint32_t len;
  uint8_t compressed;


  fd = open(argv[1], O_RDONLY);
  snprintf(outFile, OUT_FILE_NAME_LEN,"%s.decomp",argv[1]);
  fd_out = open(outFile, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU);
#ifdef TEST_LOAD_CACHE
  {
    uint64_t last_crc;
    cache = decomp_load_dre_cache ("./decomp.db", cache_sz, &cache_sz, &last_crc, 16);
    if (!cache) {
      return -1;
    }
  }
#else
  cache = new_dre_cache ("./decomp.db",cache_sz,16);
#endif
  if (!cache) {
    printf ("Can't create cache object\n");
    exit(1);
  }
  str_ctx = new_dre_stream_ctx (cache,
				0/*avg blk sz*/,
				0/*min blk sz*/,
				0/*max blk sz*/,
				0/*win sz*/);
  
  while ((sz=read(fd, &compressed, sizeof(compressed)))  != 0 ) {
    if (compressed==true) {
      uint8_t * out_buff=NULL;
      /*compressed data*/
      if ((sz=read(fd, &offset, sizeof(offset))) != sizeof(offset)) {
	printf ("Invalid file type\n");
	break;
      }
      if ((sz=read(fd, &len, sizeof(len))) != sizeof(len)) {
	printf ("Invalid file type\n");
	break;
      }
      if (rabin_decompress_block (str_ctx,
				  true, &out_buff,
				  offset, len, 1)>0) {
	write (fd_out, out_buff, len);
      }
      else {
	printf ("Error While decompressing block\n");
      }
    }/*eof compressed*/
    else if (compressed==false) {
      /*decompressed data*/
      if ((sz=read(fd, &len, sizeof(len))) != sizeof(len)) {
	printf ("Invalid file type\n");
	break;
      }
      if (buff_len < len) {
	buff = (uint8_t*) realloc (buff, len);
	if (!buff) {
	  printf ("Can't allocate buffer for reading\n");
	  break;
	}
	buff_len=len;
      }
      if ((sz=read(fd, buff, len)) != len) {
	printf ("Invalid file type\n");
	break;
      }
#ifdef TEST_FRAG_BLK
#define FRAG_SZ 128
      {
	uint32_t i;
	uint8_t * tbuf;
	for (i=0; (i+FRAG_SZ)< len; i+=FRAG_SZ) {
	  tbuf=buff+i;
	  if (rabin_decompress_block (str_ctx,
				      false, &tbuf,
				      0, FRAG_SZ, 0) > 0) {
	    write (fd_out, tbuf, FRAG_SZ);
	  }
	  else {
	    printf ("Error While decompressing block\n");
	  }
	}
	tbuf=buff+i;
	if (rabin_decompress_block (str_ctx,
				    false, &tbuf,
				    0, len-i, 1) > 0) {
	  write (fd_out, tbuf, len-i);
	}
	else {
	  printf ("Error While decompressing block\n");
	}
      }
#else
      
      if (rabin_decompress_block (str_ctx,
				  false, &buff,
				  0, len, 1)>0) {
	write (fd_out, buff, len);
      }
      else {
	printf ("Error While decompressing block\n");
      }
#endif
    }/*eof decompressed*/
    else {
      printf ("Invalid file type\n");
      break;
    }
  }/*while read*/

  free_dre_ctx(str_ctx);
  close_cache(cache);
  free (buff);
  close(fd_out);
  return 0;
}
