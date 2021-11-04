/*
    Author:     Amine Ismail
    email: Amine.Ismail@gmail.com
    
*/

#ifndef __CRC64_H__
#define __CRC64_H__

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>

extern const uint64_t g_crc64_tab[256];

#ifdef __cplusplus
extern "C" {
#endif

static inline uint64_t crc64( uint64_t crc, const unsigned char *s, uint64_t l )
{
    uint64_t j;

    for ( j = 0; j < l; j++ ) {
        crc = g_crc64_tab[(uint8_t)crc ^ s[j]] ^ (crc >> 8);
    }
    return crc;
}

static inline uint64_t crc64_step( uint64_t crc, const unsigned char s )
{
  return g_crc64_tab[(uint8_t)crc ^ s] ^ (crc >> 8);
}

#ifdef __cplusplus
};
#endif

#endif
