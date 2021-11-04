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
#include <errno.h>
#include "dre.h"
#include "crc64.h"
#include "log.h"
#define true 1
#define false 0
#define TRUE true
#define FALSE false

#define RAB_P 3
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

static uint64_t gout[MAX_WIN_SZ+1][256];

#define CACHE_IDX_SIZE(cache) \
    (cache->nb_blk_idx * sizeof(block_t) + sizeof(cache_idx_hdr_t))

//#define DEBUG_RABIN 1

#if DEBUG_RABIN
#define DBGR(format, ...) DBG(format, ##__VA_ARGS__)
#define WARNR(format, ...) WARN(format, ##__VA_ARGS__)
#else
#define DBGR(format, ...)
#define WARNR(format, ...)
#endif

#define DRE_FAKE_CACHE_SZ 1048576 //1MB

/*----------------------------------------------------------------------------*
 init_rabin_lookup_table
 *----------------------------------------------------------------------------*/
void init_rabin_lookup_table()
{
    int         i;
    uint64_t    cpow;
    uint8_t     win_sz;

    for ( win_sz = MIN_WIN_SZ; win_sz <= MAX_WIN_SZ; win_sz++ ) {
        cpow = 1;
        for ( i = 1; i < win_sz; i++ ) {
            cpow *= RAB_P;
        }
        for ( i = 0; i < 256; i++ ) {
            gout[win_sz][i] = i * cpow;
        }
    }
}


/*----------------------------------------------------------------------------*
 reset_dre_cache
 *----------------------------------------------------------------------------*/
void reset_dre_cache( dre_cache_t *cache )
{
    cache_idx_hdr_t *info = cache->cache_idx_info;

    info->cache_idx_head = 0;
    info->cache_idx_tail = 0;
    info->nb_block = 0;
    info->nb_total_block = 0;
    info->data_pos_wr = 0;
    info->last_block_size = 0;

    cache->blk_idx_head = 0;
    cache->blk_idx_tail = 0;
    cache->edge = 0;

    memset( cache->block_hash_table_head, 0, BLK_H_SIZE * sizeof(void *) );
    memset( cache->block_hash_table_tail, 0, BLK_H_SIZE * sizeof(void *) );

}


/*----------------------------------------------------------------------------*
 alloc_cache
 *----------------------------------------------------------------------------*/
static dre_cache_t *alloc_cache( const char *cache_file_name,
                                 uint16_t min_blk_size )
{
    dre_cache_t *cache;

    if ( !(cache = (dre_cache_t *)calloc( 1, sizeof(dre_cache_t) )) ) {
        return NULL;
    }
    cache->fd_cache = -1;
    cache->fd_cache_idx = -1;

    strncpy( cache->cache_file_name, cache_file_name, CACHE_NAME_SZ - 1 );
    cache->cache_file_name[CACHE_NAME_SZ - 1] = 0;

    cache->min_blk_size = min_blk_size;
    return cache;
}


/*----------------------------------------------------------------------------*
 rabin_load_cache_idx
 *----------------------------------------------------------------------------*/
static cache_idx_hdr_t *rabin_load_cache_idx( dre_cache_t *cache, int oflag, uint64_t size )
{
    char        tmp_idx_name[CACHE_NAME_SZ + 4];
    uint64_t    idx_sz;
    int         idx_fd;
    cache_idx_hdr_t *info = (void *)-1;

    snprintf( tmp_idx_name, CACHE_NAME_SZ + 4, "%s.idx", cache->cache_file_name );

    int exists = access( tmp_idx_name, R_OK|W_OK ) == 0;
    if ( !exists && !(oflag & O_CREAT) ) {
        errno = ENOENT;
        return NULL;
    }

    if ( (idx_fd = open( tmp_idx_name, oflag, S_IRUSR|S_IWUSR )) < 0 ) {
        return NULL;
    }

    if ( !exists || (oflag & O_TRUNC) ) {
        cache->nb_blk_idx = NB_BLK_IDX;
        if ( lseek( idx_fd, CACHE_IDX_SIZE(cache), SEEK_SET) == -1 ) {
            close( idx_fd );
            return NULL;
        }
        if ( write( idx_fd, "", 1 ) != 1 ) {
            close( idx_fd );
            return NULL;
        }
        lseek( idx_fd, 0L, SEEK_SET );
    } else {
        idx_sz = lseek( idx_fd, 0L, SEEK_END );
        lseek( idx_fd, 0L, SEEK_SET );
        cache->nb_blk_idx = (idx_sz - sizeof(cache_idx_hdr_t)) / sizeof(block_t);
    }

    info = (cache_idx_hdr_t *)
        mmap( (caddr_t)0, CACHE_IDX_SIZE(cache), PROT_READ|PROT_WRITE, MAP_SHARED, idx_fd, 0 );
    if ( info == (void *)-1 ) {
        close( idx_fd );
        return NULL;
    }

    cache->cache_idx_data =
        (block_t *)((cache_idx_hdr_t *)info + 1);

    memset( cache->block_hash_table_head, 0, BLK_H_SIZE * sizeof(void *) );
    memset( cache->block_hash_table_tail, 0, BLK_H_SIZE * sizeof(void *) );

    if ( !exists || (oflag & O_TRUNC) ) {
        info->cache_idx_head = 0;
        info->cache_idx_tail = 0;
        info->cache_size = size;
    } else {
        cache->edge = info->nb_total_block > info->nb_block;
#if 0
        if ( size && info->cache_size < size ) {
            WARN( "info->cache_size(%lld) < size(%lld)\n", info->cache_size, size );
            /*munmap( info, CACHE_IDX_SIZE(cache) );
            close( idx_fd );
            errno = EINVAL;
            return NULL;*/
        }
#endif
    }
    cache->fd_cache_idx = idx_fd;
    cache->cache_idx_info = info;
    return info;
}


/*----------------------------------------------------------------------------*
 rabin_load_cache_data
 *----------------------------------------------------------------------------*/
static uint8_t *rabin_load_cache_data( dre_cache_t *cache, int oflag, uint64_t size )
{
    int         data_fd;

    int exists = access( cache->cache_file_name, R_OK|W_OK ) == 0;
    if ( !exists && !(oflag & O_CREAT) ) {
        errno = ENOENT;
        return NULL;
    }

    if ( (data_fd = open( cache->cache_file_name, oflag, S_IRUSR|S_IWUSR )) < 0 ) {
        return NULL;
    }
    if ( !exists || (oflag & O_TRUNC) ) {

        if ( !(oflag & O_TRUNC) ) {
            INFO( "!exists, create\n" );
        }

        if ( !size ) {
            ERR( "empty size with O_TRUNC\n" );
            close( data_fd );
            errno = EINVAL;
            return NULL;
        }
        cache->cache_sz = size;
        if ( lseek( data_fd, cache->cache_sz - 1 , SEEK_SET) == -1 ) {
            close( data_fd );
            return NULL;
        }
        if ( write( data_fd, "", 1 ) != 1 ) {
            close( data_fd );
            return NULL;
        }
    } else {
        /* get the cache size */
        cache->cache_sz = lseek( data_fd, 0L, SEEK_END );
#if 0
        if ( size && cache->cache_sz < size ) {
            WARN( "cache->cache_sz(%lld) < size(%lld)\n", cache->cache_sz, size );
            /*close( data_fd );
            errno = EINVAL;
            return NULL;*/
        }
#endif
        lseek( data_fd, 0L, SEEK_SET );
    }
    cache->cache_data = (uint8_t *)mmap( (caddr_t)0, cache->cache_sz,
                                         PROT_READ|PROT_WRITE,
                                         MAP_SHARED, data_fd, 0 );
    if ( cache->cache_data == (void *)-1 ) {
        close( data_fd );
        return NULL;
    }
    cache->fd_cache = data_fd;
    return cache->cache_data;
}


/*--------------------------------------------------------------------*/
static inline void H_insert_bloc( dre_cache_t *cache,
                                  block_t *blk,
                                  uint64_t idx );


/*----------------------------------------------------------------------------*
 comp_load_dre_cache
 *----------------------------------------------------------------------------*/
dre_cache_t *comp_load_dre_cache( const char *cache_file_name,
                                  uint64_t cache_sz,
                                  uint64_t *total_nb_blocks,
                                  uint64_t max_block,
                                  uint16_t min_blk_size,
                                  uint8_t check_crc,
                                  uint64_t last_block_crc,
                                  uint8_t crc_size )
{
    dre_cache_t     *cache;
    uint32_t        i;
    cache_idx_hdr_t *info;
    block_t         *tmp_bloc = NULL;
    uint64_t        count;
    uint64_t        max_count;
#if DEBUG_RABIN
    uint64_t        total = 0;
#endif

    if ( !(cache = alloc_cache( cache_file_name, min_blk_size )) ) {
        return NULL;
    }

    /* Open the cache index */
    if ( !(info = rabin_load_cache_idx( cache, O_RDWR, cache_sz )) ) {
        close_cache( cache );
        return NULL;
    }

    /* open the data cache */
#ifdef DRE_COMP
    cache->cache_sz = info->cache_size;
    cache->fake_cache_sz= DRE_FAKE_CACHE_SZ;
    if ( !(cache->cache_data = (uint8_t *)malloc( DRE_FAKE_CACHE_SZ )) ) {
        ERR( "malloc(%lld)\n", (long long)cache->cache_sz );
        close_cache( cache );
        return NULL;
    }
#else
    if ( !rabin_load_cache_data( cache, O_RDWR|O_CREAT, info->cache_size ) ) {
        ERR( "rabin_load_cache_data, %d (%s)\n", errno, strerror(errno) );
        close_cache( cache );
        return NULL;
    }
#endif
    /* end open data cahe */

    count = 0;
    if ( max_block > info->nb_total_block ) {
        ERR( "max_block(%"PRIu64") > info->nb_total_block(%"PRIu64")\n",
            max_block, info->nb_total_block );
        close_cache( cache );
        errno = EINVAL;
        return NULL;
    }
    max_count = info->nb_block - (info->nb_total_block - max_block);

    if ( info->cache_idx_head < info->cache_idx_tail ) {
        for ( i = info->cache_idx_head; i < info->cache_idx_tail; i++ ) {
            if ( count >= max_count && max_block > 0 ) {
                break;
            }
            /* read block then build H table and block list */
            tmp_bloc = (block_t *)&cache->cache_idx_data[i];
#if DEBUG_RABIN
            total += tmp_bloc->block.blk_len;
            DBGR( "DRE: LOADING BLOCK: Offset=%" PRIu64
                  ", Len=%" PRIu32 ", CRC=%" PRIu64", TOTAL=%"PRIu64"B\n",
                  tmp_bloc->block.dict_offset,
                  tmp_bloc->block.blk_len,
                  tmp_bloc->block.crc, total);
#endif
            H_insert_bloc( cache, tmp_bloc, tmp_bloc->block.crc % BLK_H_SIZE );
            count++;
        }
    } else {
        for ( i = info->cache_idx_head; i < cache->nb_blk_idx; i++ ) {
            if ( count >= max_count && max_block > 0 ) {
                break;
            }
            /* read block then build H table and block list */
            tmp_bloc = (block_t *)&cache->cache_idx_data[i];
#if DEBUG_RABIN
            total += tmp_bloc->block.blk_len;
            DBGR( "DRE: LOADING BLOCK: Offset=%" PRIu64
                ", Len=%" PRIu32 ", CRC=%" PRIu64", TOTAL=%"PRIu64"B\n",
                tmp_bloc->block.dict_offset,
                tmp_bloc->block.blk_len, tmp_bloc->block.crc, total );
#endif
            H_insert_bloc( cache, tmp_bloc, tmp_bloc->block.crc % BLK_H_SIZE );
            count++;
        }
        for ( i = 0; i < info->cache_idx_tail; i++ ) {
            if ( count >= max_count && max_block > 0 ) {
                break;
            }
            /* read block then build H table and block list */
            tmp_bloc = (block_t *)&cache->cache_idx_data[i];
#if DEBUG_RABIN
            total += tmp_bloc->block.blk_len;
            DBGR( "DRE: LOADING BLOCK: Offset=%" PRIu64
                ", Len=%" PRIu32 ", CRC=%" PRIu64", TOTAL=%"PRIu64"B\n",
                tmp_bloc->block.dict_offset,
                tmp_bloc->block.blk_len, tmp_bloc->block.crc, total );
#endif
            H_insert_bloc( cache, tmp_bloc, tmp_bloc->block.crc % BLK_H_SIZE );
            count++;
        }
    }

    if ( check_crc ) {
        uint64_t mask = (1ULL << crc_size) - 1;
        if ( (tmp_bloc->block.crc & mask) != (last_block_crc & mask) ) {
            ERR( "DRE: Load cache failed CRC does not match\n" );
            close_cache (cache);
            errno = EINVAL;
            return NULL;
        }
    }

    info->cache_idx_tail = i;

    if ( max_block ) {
        info->nb_block -= info->nb_total_block - max_block;
        info->nb_total_block = max_block;
    }

    cache->cache_idx_info->data_pos_wr =
        tmp_bloc->block.dict_offset + tmp_bloc->block.blk_len;

    DBGR( "DRE: Cache loaded: size=%" PRIu64 ", nb_block=%" PRIu64
        ", total_block=%" PRIu64 ", Loaded=%" PRIu64
        ", Edge=%" PRIu8 " Last CRC=%" PRIu64 "\n",
        cache->cache_sz, info->nb_block, info->nb_total_block,
        count, cache->edge, tmp_bloc->block.crc );

    *total_nb_blocks = info->nb_total_block;
    return cache;
}


/*----------------------------------------------------------------------------*
 decomp_load_dre_cache
 *----------------------------------------------------------------------------*/
dre_cache_t *decomp_load_dre_cache( const char *cache_file_name,
                                    uint64_t cache_sz,
                                    uint64_t *total_nb_blocks,
                                    uint64_t *last_block_crc,
                                    uint16_t min_blk_size )
{
    dre_cache_t     *cache;
    cache_idx_hdr_t *info;

    if ( !(cache = alloc_cache( cache_file_name, min_blk_size )) ) {
        return NULL;
    }

    if ( !rabin_load_cache_data( cache, O_RDWR, cache_sz ) ) {
        return NULL;
    }

    /* Open the cache index */
    if ( !(info = rabin_load_cache_idx( cache, O_RDWR, cache_sz )) ) {
        close_cache( cache );
        return NULL;
    }

    *total_nb_blocks = info->nb_total_block;

    /* compute the CRC of the last block */
    *last_block_crc =
        crc64( 0, &cache->cache_data[info->data_pos_wr - info->last_block_size],
               info->last_block_size );

    DBGR( "DRE: Cache loaded: size=%" PRIu64 ", nb_block=%" PRIu64
        ", total_block=%" PRIu64
        " Last_CRC=%" PRIu64 "\n",
        cache->cache_sz, info->nb_block,
        info->nb_total_block, *last_block_crc );

    return cache;
}


/*----------------------------------------------------------------------------*
 new_dre_cache
 *----------------------------------------------------------------------------*/
dre_cache_t *new_dre_cache( const char *cache_file_name,
                            uint64_t cache_sz,
			                uint16_t min_blk_size )
{
    dre_cache_t *cache;

    if ( !(cache = alloc_cache( cache_file_name, min_blk_size )) ) {
        return NULL;
    }

    /* cache file */
#ifdef DRE_COMP
    if ( !(cache->cache_data = (uint8_t *)malloc( DRE_FAKE_CACHE_SZ )) ) {
        free( cache );
        return NULL;
    }
    cache->fake_cache_sz = DRE_FAKE_CACHE_SZ;
    cache->cache_sz = cache_sz;
#else
    if ( !rabin_load_cache_data( cache, O_RDWR|O_CREAT|O_TRUNC, cache_sz ) ) {
        return NULL;
    }
#endif

    /* bloc index file */
    if ( !rabin_load_cache_idx( cache, O_RDWR|O_CREAT|O_TRUNC, cache_sz ) ) {
        close_cache( cache );
        return NULL;
    }

    return cache;
}


/*----------------------------------------------------------------------------*
 new_dre_stream_ctx
 *----------------------------------------------------------------------------*/
dre_stream_t *new_dre_stream_ctx( dre_cache_t *cache,
                                  uint64_t avg_sz_blk_bits,
                                  uint32_t min_blk_sz,
                                  uint32_t max_blk_sz,
                                  uint8_t win_sz)
{
    dre_stream_t *ctx = (dre_stream_t *)malloc( sizeof(dre_stream_t) );
    if ( !ctx ) {
        return NULL;
    }
    memset( ctx, 0, sizeof(dre_stream_t) );
    ctx->avg_blk_size_bit_mask = (uint64_t)~((~0x0ULL) << avg_sz_blk_bits);
    ctx->cache = cache;
    ctx->min_blk_sz = min_blk_sz;
    ctx->max_blk_sz = max_blk_sz;
    ctx->win_sz = win_sz;

    return ctx;
}


/*----------------------------------------------------------------------------*
 H_insert_bloc
 *----------------------------------------------------------------------------*/
static inline void H_insert_bloc( dre_cache_t * cache, block_t * blk, uint64_t idx )
{
    blk->l_nxt = NULL;
    blk->l_prv = NULL;
    blk->h_nxt = NULL;
    blk->h_prv = NULL;

    if ( cache->block_hash_table_head[idx] == NULL ) {
        /*first*/
        cache->block_hash_table_head[idx] = cache->block_hash_table_tail[idx]=blk;
    } else {
        cache->block_hash_table_tail[idx]->h_nxt = blk;
        blk->h_prv = cache->block_hash_table_tail[idx];
        cache->block_hash_table_tail[idx] = blk;
    }
    /*------------------------------------*/
    if ( cache->blk_idx_head == NULL ) {
        cache->blk_idx_head = cache->blk_idx_tail = blk;
    } else {
        cache->blk_idx_tail->l_nxt = blk;
        blk->l_prv = cache->blk_idx_tail;
        cache->blk_idx_tail = blk;
    }
}

static inline void remove_oldest_block( dre_cache_t * cache );


/*----------------------------------------------------------------------------*
 new_block_info
 *----------------------------------------------------------------------------*/
static inline block_t *new_block_info( dre_stream_t * ctx )
{
    dre_cache_t * cache = ctx->cache;
    block_t *ret;
    if ( cache->edge 
      && cache->cache_idx_info->cache_idx_tail == cache->cache_idx_info->cache_idx_head ) {
        remove_oldest_block( cache );
    }
    ret = &cache->cache_idx_data[cache->cache_idx_info->cache_idx_tail];

    if ((cache->cache_idx_info->cache_idx_tail + 1) == cache->nb_blk_idx) {
      cache->edge = 1;
    }
    cache->cache_idx_info->cache_idx_tail =
            (cache->cache_idx_info->cache_idx_tail + 1) % cache->nb_blk_idx;

    return ret;
}


/*----------------------------------------------------------------------------*
 remove_oldest_block
 *----------------------------------------------------------------------------*/
static inline void remove_oldest_block( dre_cache_t * cache )
{
    block_t     *tmp_blk;
    uint64_t    hidx;

    /* remove (blk_idx_head) from the following structures
     1- block_hash_table head/tail
     2- blk_idx head/tail
     3- cache_idx_data : should be the fist one
     */

    tmp_blk = cache->blk_idx_head;

    DBGR( "DRE: removing block : Offset=%" PRIu64 ", len=%" PRIu32 ", crc=%" PRIu64 "\n",
            tmp_blk->block.dict_offset,
            tmp_blk->block.blk_len,
            tmp_blk->block.crc);
    DBGR( "DRE: Cache idx head=%" PRIu32 ", Cache idx tail=%" PRIu32 ", NB block=%" PRIu64 "\n",
            cache->cache_idx_info->cache_idx_head,
            cache->cache_idx_info->cache_idx_tail,
            cache->cache_idx_info->nb_block);

    if ( cache->blk_idx_head ) {
        cache->blk_idx_head = cache->blk_idx_head->l_nxt;
    }

    hidx = tmp_blk->block.crc % BLK_H_SIZE;
    if ( tmp_blk->h_nxt == NULL ) {
        /* last */
        cache->block_hash_table_tail[hidx] = tmp_blk->h_prv;
        if ( cache->block_hash_table_tail[hidx] ) {
            cache->block_hash_table_tail[hidx]->h_nxt = NULL;
        }
    } else {
        tmp_blk->h_nxt->h_prv = tmp_blk->h_prv;
    }
    if ( tmp_blk->h_prv == NULL ) {
        /* first */
        cache->block_hash_table_head[hidx] = tmp_blk->h_nxt;
        if ( cache->block_hash_table_head[hidx] ) {
            cache->block_hash_table_head[hidx]->h_prv = NULL;
        }
    } else {
        tmp_blk->h_prv->h_nxt = tmp_blk->h_nxt;
    }
    cache->cache_idx_info->nb_block--;
    /*change the head position in the index file */
    cache->cache_idx_info->cache_idx_head =
            (cache->cache_idx_info->cache_idx_head + 1) % cache->nb_blk_idx;
}


/*----------------------------------------------------------------------------*
 new_bloc
 *----------------------------------------------------------------------------*/
static inline void new_bloc( dre_stream_t * ctx,
                             uint32_t blk_len, uint64_t fp,
                             uint64_t crc, uint64_t dict_offset )
{

    block_t     *tmp_bloc;

    tmp_bloc = new_block_info( ctx );
    tmp_bloc->l_nxt = tmp_bloc->l_prv = tmp_bloc->h_nxt = tmp_bloc->h_prv = NULL;
    tmp_bloc->block.blk_len = blk_len;
    tmp_bloc->block.fp = fp;
    tmp_bloc->block.crc = crc;
    tmp_bloc->block.dict_offset = dict_offset;
    DBGR( "DRE: N Offset=%" PRIu64 ", Len=%" PRIu32 ", CRC=%" PRIu64"\n",
        tmp_bloc->block.dict_offset,
        tmp_bloc->block.blk_len,
        crc );
    H_insert_bloc( ctx->cache, tmp_bloc, crc % BLK_H_SIZE );

    ctx->cache->cache_idx_info->nb_block++;
    ctx->cache->cache_idx_info->nb_total_block++;
}


/*----------------------------------------------------------------------------*
 H_lookup
 *----------------------------------------------------------------------------*/
static inline block_t *H_lookup( dre_stream_t * ctx, uint64_t fp,
                                 uint64_t crc, uint32_t len )
{
    block_t *parc;

    parc = ctx->cache->block_hash_table_head[crc % BLK_H_SIZE];
    while ( parc != NULL ) {
        if ( parc->block.crc == crc
          && parc->block.fp == fp
          && parc->block.blk_len == len ) {
            return parc;
        }
        parc = parc->h_nxt;
    }
    return NULL;
}


/*----------------------------------------------------------------------------*
 H_lookup
 *----------------------------------------------------------------------------*/
static inline uint64_t write_block_to_cache( dre_stream_t *ctx, uint8_t *data,
                                             uint64_t len, uint8_t do_write, uint32_t * data_pos)
{
  uint64_t pos;
  
#ifdef DRE_COMP
  if (ctx->cache->fake_pos+len > ctx->cache->fake_cache_sz) {
    if ((ctx->cache->cache_data = (uint8_t*) realloc (ctx->cache->cache_data,
						      ctx->cache->fake_pos+len))==NULL){
      /* FIXME: Process error */
      ERR( "realloc(%lld)\n", (long long)ctx->cache->fake_pos+len );
    }
    else {
      ctx->cache->fake_cache_sz=ctx->cache->fake_pos+len;
    }
  }
  
  if (data_pos) {
   *data_pos = ctx->cache->fake_pos;
  }
  memcpy( ctx->cache->cache_data + ctx->cache->fake_pos, data, len);
  ctx->cache->fake_pos+=len;
#else

  memcpy( ctx->cache->cache_data + ctx->cache->cache_idx_info->data_pos_wr, data, len);
  
#endif
  pos = ctx->cache->cache_idx_info->data_pos_wr;
  ctx->cache->cache_idx_info->data_pos_wr += len;

  return pos;
}


/*----------------------------------------------------------------------------*
 new_out_buff
 *----------------------------------------------------------------------------*/
static inline c_buff_t *new_out_buff( dre_stream_t * ctx, uint8_t comp,
                                      uint64_t dict_offset,
                                      uint32_t len, uint64_t crc64 )
{
    c_buff_t *tmp;

    if ( !(tmp = (c_buff_t *)malloc( sizeof(c_buff_t) )) ) {
        return NULL;
    }
    tmp->nxt = NULL;
    tmp->flags = 0;
    tmp->compressed = comp;
    tmp->dict_offset = dict_offset;
    tmp->data = ctx->cache->cache_data + dict_offset;
    tmp->blk_len = len;
    tmp->crypto_h = crc64;
    return tmp;
}


/*----------------------------------------------------------------------------*
 insert_block_out
 *----------------------------------------------------------------------------*/
static inline void insert_block_out( c_buff_t *tmp, c_buff_t **out_head,
                                     c_buff_t **out_tail )
{
    if ( !*out_head ) {
        *out_head = *out_tail = tmp;
    } else {
        (*out_tail)->nxt = tmp;
        *out_tail = tmp;
    }
}


/*----------------------------------------------------------------------------*
 insert_block_out
 *----------------------------------------------------------------------------*/
static inline void edge_cache( dre_cache_t * cache, uint32_t len )
{
#if DEBUG_RABIN
    static int nb_rot = 0;
#endif
    uint8_t cache_rotate = cache->cache_idx_info->data_pos_wr + len > cache->cache_sz;
    if ( cache_rotate ) {
#if DEBUG_RABIN
        nb_rot++;
        DBGR( "DRE: ======= Rotate cache %d======== \n", nb_rot );
#endif
        while ((cache->blk_idx_head->block.dict_offset >=cache->cache_idx_info->data_pos_wr) &&
	       (cache->blk_idx_head->block.dict_offset != 0)) {
            remove_oldest_block( cache );
        }
        cache->cache_idx_info->data_pos_wr = 0;
        cache->edge = 1;
    }
    if ( cache->edge ) {
        while ( cache->blk_idx_head->block.dict_offset
              - cache->cache_idx_info->data_pos_wr < len ) {
            remove_oldest_block( cache );
        }
    }
}


/*----------------------------------------------------------------------------*
 edge_cache_decomp
 *----------------------------------------------------------------------------*/
static inline void edge_cache_decomp( dre_cache_t * cache, uint32_t len )
{
    uint8_t cache_rotate = cache->cache_idx_info->data_pos_wr + len > cache->cache_sz;

    if ( cache_rotate ) {
        cache->cache_idx_info->data_pos_wr = 0;
        DBGR( "DRE: ======= Rotate cache ======== \n" );
    }
}


/*----------------------------------------------------------------------------*
 rabin_compress_buffer
 *----------------------------------------------------------------------------*/
c_buff_t *rabin_compress_buffer( dre_stream_t *ctx, uint8_t *buff, uint64_t sz )
{
    int         i;
    uint8_t     pushed_out;
    block_t     *hblk;
    uint64_t    offset;
    c_buff_t    *out_head = NULL, *out_tail = NULL, *tmp = NULL;
    uint64_t    blk_start = 0;
    uint64_t    blk_len = 0;

    for ( i = 0; i < sz; i++ ) {
        ctx->blk_len++;
        blk_len++;
        if ( ctx->idx >= ctx->win_sz ) {
            if ( i < ctx->win_sz ) {
                pushed_out = ctx->tmp_win[i];
            } else {
                pushed_out = buff[i - ctx->win_sz];
            }
            ctx->fp -= gout[ctx->win_sz][pushed_out];
        }
        ctx->fp *= RAB_P;
        ctx->fp += buff[i];
        ctx->crc = crc64_step( ctx->crc, buff[i] );
        ctx->idx++;

        if ( ctx->blk_len < ctx->min_blk_sz ) {
            continue;
        }

        if ( (ctx->fp & ctx->avg_blk_size_bit_mask) == 0
          || (ctx->max_blk_sz > 0 && ctx->blk_len >= ctx->max_blk_sz) ) {
            /* check this section against the cache */
            if ( (hblk = H_lookup (ctx, ctx->fp, ctx->crc, ctx->blk_len)) != NULL ) {
#if 0
                DBGR( "DRE: REDUNDANCY FOUND: Offset=%" PRIu64 ", Len=%" PRIu32 " CRC=%" PRIu64"\n",
                        hblk->block.dict_offset,hblk->block.blk_len, ctx->crc );
#endif
                DBGR( "DRE: C Offset=%" PRIu64 ", Len=%" PRIu32 ", CRC=%" PRIu64"\n",
                        hblk->block.dict_offset,hblk->block.blk_len, hblk->block.crc );

                tmp = new_out_buff( ctx, true, hblk->block.dict_offset,
                                    hblk->block.blk_len, hblk->block.crc );
                /* FIXME: check result */
            } else {
	      uint32_t data_pos=0;
                /*
                   Here a new block is formed
                   this block should be written to cache.
                   meta data should be updated to
                 */
                /* Check if we should edge */
                edge_cache( ctx->cache,  ctx->blk_len );
                /* write block in the cache */
                if ( ctx->tmp_blk_len ) {
		  offset = write_block_to_cache( ctx, ctx->tmp_blk, ctx->tmp_blk_len, 0, &data_pos );
		  write_block_to_cache( ctx, &buff[blk_start], blk_len, 0, 0);
                } else {
		  offset = write_block_to_cache( ctx, &buff[blk_start], blk_len, 0, &data_pos );
                }
                /* update meta-data */
                new_bloc( ctx, ctx->blk_len, ctx->fp, ctx->crc, offset );
#ifdef DRE_COMP
                tmp = new_out_buff(ctx, false, data_pos, ctx->blk_len, ctx->crc );
#else
		tmp = new_out_buff(ctx, false, offset, ctx->blk_len, ctx->crc );
#endif
                /* FIXME: check result */
                //DBGR("New Block: Offset=%"PRIu64", Len=%"PRIu32", RFP=%"PRIu64", Crypto=%"PRIu64"\n",
                //	offset, ctx->blk_len, ctx->fp, ctx->crc);
            }
            insert_block_out( tmp, &out_head, &out_tail );
            ctx->blk_len = 0;
            blk_len = 0;
            ctx->blk_start = ctx->idx;
            blk_start = i + 1;
            ctx->crc = 0;
            ctx->tmp_blk_len = 0;
        }
    }
    /*copy last win_sz byte*/
#if 0
    {
        uint8_t tmp_win_sz = 0;
        tmp_win_sz = sz > ctx->win_sz ? ctx->win_sz : sz;
        memmove( ctx->tmp_win, &ctx->tmp_win[tmp_win_sz], ctx->win_sz-tmp_win_sz );
        memcpy( &ctx->tmp_win[ctx->win_sz-tmp_win_sz], &buff[sz-tmp_win_sz], tmp_win_sz );
    }
#else
    if ( sz < ctx->win_sz ) {
        memmove( ctx->tmp_win, &ctx->tmp_win[sz], ctx->win_sz-sz );
        memcpy( &ctx->tmp_win[ctx->win_sz-sz], buff, sz );
    } else {
        memcpy( ctx->tmp_win, &buff[sz-ctx->win_sz], ctx->win_sz );
    }
#endif
    ctx->tmp_blk = (uint8_t *)realloc( ctx->tmp_blk, ctx->tmp_blk_len + sz - blk_start );
    if ( !ctx->tmp_blk && (ctx->tmp_blk_len + sz - blk_start) ) {
        // FIXME: report error
        ERR( "realloc(%lld)\n", (long long)(ctx->tmp_blk_len + sz - blk_start) );
    }
    memcpy( ctx->tmp_blk + ctx->tmp_blk_len, &buff[blk_start], sz-blk_start);
    ctx->tmp_blk_len = sz - blk_start + ctx->tmp_blk_len;
#ifdef DRE_COMP
    ctx->cache->fake_pos=0;
#endif
    return out_head;
}



/*----------------------------------------------------------------------------*
 rabin_flush
 *----------------------------------------------------------------------------*/
c_buff_t *rabin_flush( dre_stream_t *ctx )
{
    c_buff_t    *tmp = NULL;
    uint64_t    offset;
    block_t     *hblk;

    if ( ctx->tmp_blk_len ) {
        if ( ctx->tmp_blk_len >= ctx->cache->min_blk_size ) {
            if ( (hblk = H_lookup( ctx, ctx->fp, ctx->crc, ctx->tmp_blk_len )) ) {
                tmp = new_out_buff( ctx, true, hblk->block.dict_offset,
                                    hblk->block.blk_len, hblk->block.crc );
                /* FIXME: check result */
                if ( hblk->block.blk_len != ctx->tmp_blk_len ) {
                    INFO ("DRE: ERROR CRC=%" PRIu64 " fp=%" PRIu64 " len=%" PRIu32 "\n",
                        ctx->fp, ctx->crc, ctx->tmp_blk_len );
                }
#if 0
                DBGR( "DRE: REDUNDANCY FOUND: Offset=%" PRIu64 ", Len=%" PRIu32 " CRC=%" PRIu64"\n",
                    hblk->block.dict_offset, hblk->block.blk_len, ctx->crc );
#endif
                DBGR( "DRE: FC Offset=%" PRIu64 ", Len=%" PRIu32 ", CRC=%" PRIu64"\n",
                    hblk->block.dict_offset, hblk->block.blk_len, ctx->crc );
            }
            else {
	        uint32_t data_pos=0;
                /* Check if we should edge */
                edge_cache( ctx->cache, ctx->tmp_blk_len );
                offset = write_block_to_cache( ctx, ctx->tmp_blk, ctx->tmp_blk_len, 0, &data_pos );
                new_bloc( ctx, ctx->tmp_blk_len, ctx->fp, ctx->crc, offset );
#ifdef DRE_COMP
                tmp = new_out_buff( ctx, false, data_pos, ctx->tmp_blk_len, ctx->crc );
#else
		tmp = new_out_buff( ctx, false, offset, ctx->tmp_blk_len, ctx->crc );
#endif
                /* FIXME: check result */
            }
        } else {
            /* do not insert this block in the cache */
            DBGR( "DRE: FI (blk too smalll %"PRIu32")\n", ctx->tmp_blk_len );
            tmp = new_out_buff( ctx, false, 0,ctx->tmp_blk_len, ctx->crc );
            /* FIXME: check result */
            tmp->dict_offset = 0;
            tmp->data = ctx->tmp_blk;

        }
        ctx->tmp_blk_len = 0;
        ctx->blk_start = ctx->idx;
        ctx->crc = 0;
        ctx->blk_len = 0;
    }
#ifdef DRE_COMP
    ctx->cache->fake_pos=0;
#endif
    return tmp;
}


/*----------------------------------------------------------------------------*
 rabin_decompress_block
 *----------------------------------------------------------------------------*/
int rabin_decompress_block( dre_stream_t * ctx,
                            uint8_t compressed, uint8_t ** buff,
                            uint64_t offset, uint32_t len, uint8_t last )
{
#if DEBUG_RABIN
    uint64_t pos;
#endif
    if ( !compressed ) {

        if ( len + ctx->cache->tmp_block_len > ctx->cache->tmp_buffer_len ) {
            ctx->cache->tmp_buffer = (uint8_t *)realloc( ctx->cache->tmp_buffer,
                (len + ctx->cache->tmp_block_len) * sizeof(*ctx->cache->tmp_buffer) );
            if ( !ctx->cache->tmp_buffer ) {
                ERR( "realloc(%lld)\n", (long long)(len + ctx->cache->tmp_block_len) * sizeof(*ctx->cache->tmp_buffer) );
                return 0;
            }
            ctx->cache->tmp_buffer_len = len + ctx->cache->tmp_block_len;
        }
        memcpy( ctx->cache->tmp_buffer + ctx->cache->tmp_block_len, *buff, len );
        ctx->cache->tmp_block_len += len;

        if ( last ) {
            if ( ctx->cache->tmp_block_len >= ctx->cache->min_blk_size ) {

                edge_cache_decomp( ctx->cache, ctx->cache->tmp_block_len );
#if DEBUG_RABIN
                pos=
#endif
                write_block_to_cache( ctx, ctx->cache->tmp_buffer,
                                      ctx->cache->tmp_block_len, 1,0 );

                ctx->cache->cache_idx_info->last_block_size = ctx->cache->tmp_block_len;
                ctx->cache->cache_idx_info->nb_total_block++;
                ctx->cache->cache_idx_info->nb_block++;

#if 0
                DBGR( "DRE: new uncompressed block Offset=%"PRIu64", len=%"PRIu32"\n", pos, len );
#endif
                DBGR( "DRE: N Offset=%"PRIu64", Len=%"PRIu32"\n", pos, len );
            } else {
                DBGR( "DRE: NI (block too small %"PRIu32")\n", ctx->cache->tmp_block_len );
            }
            ctx->cache->tmp_block_len = 0;
        }
        return len;
    } else if ( compressed == 1 ) {
#if 0
        DBGR("DRE: new compressed block Offset=%"PRIu64", Len=%"PRIu32"\n", offset, len );
#endif
        DBGR("DRE: C Offset=%"PRIu64", Len=%"PRIu32"\n", offset, len );
        *buff = ctx->cache->cache_data + offset;
        return len;
    } else {
        DBGR( "DRE: Unknown compression type: %d\n", compressed );
        return 0;
    }
}


/*----------------------------------------------------------------------------*
 close_cache
 *----------------------------------------------------------------------------*/
void close_cache( dre_cache_t *cache )
{
    if ( cache ) {
        if ( cache->cache_idx_info && cache->cache_idx_info != (void *)-1 ) {
            munmap( cache->cache_idx_info, CACHE_IDX_SIZE(cache) );
        }
        if ( cache->fd_cache_idx >= 0 ) {
            close( cache->fd_cache_idx );
        }
#ifdef DRE_COMP
        if ( cache->cache_data ) {
            free( cache->cache_data );
        }
#else
        if ( cache->cache_data && cache->cache_data != (void *)-1 ) {
            munmap( cache->cache_data, cache->cache_sz );
        }
        if ( cache->fd_cache >= 0 ) {
            close( cache->fd_cache );
        }
#endif
        if ( cache->tmp_buffer && cache->tmp_buffer_len ) {
            free( cache->tmp_buffer );
        }
        free( cache );
    }
}


/*----------------------------------------------------------------------------*
 free_dre_ctx
 *----------------------------------------------------------------------------*/
void free_dre_ctx( dre_stream_t *ctx )
{
    if ( ctx->tmp_blk ) {
        free( ctx->tmp_blk );
    }
    free( ctx );
}

