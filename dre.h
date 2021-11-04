#ifndef __RABIN_H__
#define __RABIN_H__

#include <stdint.h>

#define RAB_MAX_WIN_SIZE 64
#ifdef DRE_DECOMP
#define NB_BLK_IDX 2
#define BLK_H_SIZE NB_BLK_IDX
#else
#define NB_BLK_IDX 131072 /*2^17*/
#define BLK_H_SIZE NB_BLK_IDX
#endif
#define CACHE_NAME_SZ 256
#define MAX_WIN_SZ 64
#define MIN_WIN_SZ 3

typedef struct c_buff_t {
  uint8_t compressed;
  uint64_t dict_offset;
  uint8_t * data;
  uint32_t blk_len;
  uint64_t crypto_h;
  struct c_buff_t * nxt;
  uint32_t flags;
} c_buff_t;

typedef struct block_idx_t {
  uint64_t dict_offset;
  uint32_t blk_len;
  uint64_t fp;
  uint64_t crc;
}block_idx_t;

typedef struct block_t {
  block_idx_t block;
  struct block_t * h_nxt; 
  struct block_t * h_prv; 
  struct block_t * l_nxt;
  struct block_t * l_prv;
}block_t;

typedef struct cache_idx_hdr_t {
  uint32_t cache_idx_head; // oldest block pos
  uint32_t cache_idx_tail; // latest block pos
  uint64_t nb_block;       // number of block currently in cache
  uint64_t nb_total_block; // total number of block since start
  uint64_t data_pos_wr;
  uint64_t cache_size;
  uint32_t last_block_size;// The size of the last cached block (used by decomp)
}cache_idx_hdr_t;

typedef struct dre_cache_t {
  int fd_cache;
  char cache_file_name [CACHE_NAME_SZ];
  uint8_t * cache_data;
  int fd_cache_idx;
  block_t * cache_idx_data;
  cache_idx_hdr_t * cache_idx_info;
  uint64_t cache_sz;
#ifdef DRE_COMP
  uint32_t fake_cache_sz;
  uint32_t fake_pos;
#endif
  uint32_t nb_blk_idx;
  uint8_t edge;
  uint8_t * tmp_buffer;
  uint32_t tmp_block_len;
  uint32_t tmp_buffer_len;
  uint16_t min_blk_size;
  block_t * block_hash_table_head [BLK_H_SIZE];
  block_t * block_hash_table_tail [BLK_H_SIZE];  
  block_t * blk_idx_head;
  block_t * blk_idx_tail;
}dre_cache_t;

typedef struct dre_stream_t {
  uint64_t id;
  uint64_t idx;
  uint64_t blk_start;
  uint32_t blk_len;
  uint32_t min_blk_sz;
  uint32_t max_blk_sz;
  uint64_t fp;
  uint8_t win_sz;
  uint64_t avg_blk_size_bit_mask;
  dre_cache_t * cache;
  uint64_t crc;
  uint8_t * tmp_blk;
  uint8_t incomplete_block;
  uint32_t tmp_blk_len;
  uint8_t tmp_header[16];
  uint8_t tmp_header_len;
  uint8_t tmp_hearder_compressed;
  uint8_t incomplete_hdr;
  uint8_t tmp_win [RAB_MAX_WIN_SIZE];
} dre_stream_t;


#define DEFAULT_CACHE_IDX_SIZE \
 (NB_BLK_IDX * sizeof(block_t) + sizeof(cache_idx_hdr_t))

/*---------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

  void init_rabin_lookup_table();
  dre_cache_t *comp_load_dre_cache( const char *cache_file_name,
				     uint64_t cache_sz,
				     uint64_t *total_nb_blocks,
				     uint64_t max_block,
				     uint16_t min_blk_size,
				     uint8_t check_crc,
				     uint64_t last_block_crc,
				     uint8_t crc_size );
  dre_cache_t *decomp_load_dre_cache( const char *cache_file_name,
				       uint64_t cache_sz,
				       uint64_t *total_nb_blocks,
				       uint64_t *last_block_crc,
				       uint16_t min_blk_size );
  dre_cache_t *new_dre_cache( const char *cache_file_name, uint64_t cache_sz,
			       uint16_t min_blk_size );
  dre_stream_t *new_dre_stream_ctx( dre_cache_t *cache,
				     uint64_t avg_sz_blk_bits,
				     uint32_t min_blk_sz,
				     uint32_t max_blk_sz,
				     uint8_t win_sz );
  
  c_buff_t *rabin_compress_buffer( dre_stream_t * ctx, uint8_t * buff,
				    uint64_t sz );
  int rabin_decompress_block( dre_stream_t * ctx,
			      uint8_t compressed, uint8_t ** buff,
			      uint64_t offset, uint32_t len, uint8_t last );

  c_buff_t *rabin_flush( dre_stream_t * ctx );
  void close_cache( dre_cache_t * cache );
  void free_dre_ctx( dre_stream_t * ctx );
  void reset_dre_cache( dre_cache_t * cache );

#ifdef __cplusplus
};
#endif

#endif
