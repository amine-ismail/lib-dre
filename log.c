/*

  Author:     Amine Ismail
  Email :     Amine.Ismail@gmail.com
*/


#include "log.h"

#if __ANDROID__
#include <android/log.h>
#define TAG "DRE"
#else
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <time.h>
#include <sys/time.h>
#endif
#include <stdarg.h>
#include <string.h>
#include <errno.h>

static char         g_running_level = PROXY_LOG_INFO;
static int          g_id;
static int          g_syslog;
static f_localtime  *g_localtime = localtime_r;
static  FILE        *g_log_file;

#define RED    "31"
#define GREEN  "32"
#define ORANGE "33"
#define BLUE   "34"
#define NCOLOR "0"

#define clearscreen() fprintf( stderr, "\033[H\033[2J" )
#define color(param) fprintf( stderr, "\033[%sm", param )
#define scolor(out,param) sprintf( out, "\033[%sm", param )

/*----------------------------------------------------------------------------*
 set_logger_level
 *----------------------------------------------------------------------------*/
void set_logger_level( int level )
{
    g_running_level = level;
    if ( g_syslog ) {
        setlogmask( LOG_UPTO( level ) );
    }
}


/*----------------------------------------------------------------------------*
 get_logger_level
 *----------------------------------------------------------------------------*/
int get_logger_level()
{
    return g_running_level;
}


/*----------------------------------------------------------------------------*
 set_logger_localtime:
 *----------------------------------------------------------------------------*/
void set_logger_localtime( f_localtime *func )
{
    g_localtime = func;
}


/*----------------------------------------------------------------------------*
 set_logger_log_file:
 *----------------------------------------------------------------------------*/
void set_logger_log_file( FILE *file )
{
    g_log_file = file;
}


/*----------------------------------------------------------------------------*
 set_logger_id: for forked processes, add an id to the logs
 *----------------------------------------------------------------------------*/
void set_logger_id( int id )
{
    g_id = id;
}


/*----------------------------------------------------------------------------*
 set_logger_syslog: use syslog instead of stderr
 *----------------------------------------------------------------------------*/
void set_logger_syslog( int syslog )
{
    if ( g_syslog != syslog ) {
        if ( (g_syslog = syslog) ) {
            setlogmask( LOG_UPTO( g_running_level ) );
        }
    }
}


/*----------------------------------------------------------------------------*
 get_logger_syslog
 *----------------------------------------------------------------------------*/
int get_logger_syslog()
{
    return g_syslog;
}

/*----------------------------------------------------------------------------*
 add_log_header
 *----------------------------------------------------------------------------*/
static int add_log_header( int level, char *fmt, int max, const char *format )
{
    char *f = fmt;

    struct timeval tv;
    struct tm nowtm;
    gettimeofday( &tv, NULL );
    (*g_localtime)( (time_t *)&tv.tv_sec, &nowtm );

    switch ( level ) {
        case PROXY_LOG_ERROR:
            f += scolor( f, RED );
            break;
        case PROXY_LOG_WARNING:
            f += scolor( f, ORANGE );
            break;
        case PROXY_LOG_INFO:
            f += scolor( f, GREEN );
            break;
        case PROXY_LOG_DEBUG:
            break;
        default:
            f += scolor( f, NCOLOR );
            break;
    }

    if ( g_id ) {
        f += sprintf( f, "[%d] %02d:%02d:%02d.%03d ",
                g_id, nowtm.tm_hour, nowtm.tm_min, nowtm.tm_sec,
                (int)(tv.tv_usec / 1000) );
    } else {
        f += sprintf( f, "%02d:%02d:%02d.%03d ",
                nowtm.tm_hour, nowtm.tm_min, nowtm.tm_sec,
                (int)(tv.tv_usec / 1000) );
    }

    int l = strlen( format );
    if ( l >= max - (f - fmt) ) {
        l = max - (f - fmt) - 1;
    }
    memcpy( f, format, l );
    f += l;

    if ( 6 < max - (f - fmt) ) {
        if ( level != PROXY_LOG_DEBUG ) {
            f += scolor( f, NCOLOR );
        }
    }
    f[0] = 0;
    return strlen( fmt );
}


/*----------------------------------------------------------------------------*
 logger
 *----------------------------------------------------------------------------*/
int logger( uint8_t level, const char *format, ... )
{
    va_list arg;
    int     save_errno = errno;

#if __ANDROID__
    if ( g_running_level >= level ) {
        if ( g_log_file ) {
            char fmt[4096];
            add_log_header( level, fmt, 4096, format );
            va_start( arg, format );
            vfprintf( g_log_file, fmt, arg );
            va_end( arg );
            fflush( g_log_file );
        }
        switch ( level ) {
            case PROXY_LOG_ERROR:
                level = ANDROID_LOG_ERROR;
                break;
            case PROXY_LOG_WARNING:
                level = ANDROID_LOG_WARN;
                break;
            case PROXY_LOG_INFO:
                level = ANDROID_LOG_INFO;
                break;
            case PROXY_LOG_DEBUG:
                level = ANDROID_LOG_DEBUG;
                break;
            default:
                level = ANDROID_LOG_VERBOSE;
        }
        va_start( arg, format );
        __android_log_vprint( level, TAG, format, arg );
        va_end( arg );
    }
#else
    if ( g_syslog ) {
        va_start( arg, format );
        vsyslog( g_running_level, format, arg );
        va_end( arg );
    } else if ( g_running_level >= level ) {

        char fmt[4096];
        add_log_header( level, fmt, 4096, format );

        va_start( arg, format );
        vfprintf( stderr, fmt, arg );
        va_end( arg );

        if ( g_log_file ) {
            va_start( arg, format );
            vfprintf( g_log_file, fmt, arg );
            va_end( arg );
            fflush( g_log_file );
        }
        /* fflush( stderr ); */
    }
#endif
    errno = save_errno;
    return 0;
}


#ifdef __ANDROID__
#if DEBUG
#define PRINT(format, ...) __android_log_print( ANDROID_LOG_DEBUG, TAG, \
                                                format, ##__VA_ARGS__)
#else
#define PRINT(format, ...)
#endif
#else
#define PRINT(format, ...) DBG(format, ##__VA_ARGS__)
#endif

/*----------------------------------------------------------------------------*
 logHexDump
 *----------------------------------------------------------------------------*/
void logHexDump( char *desc, void *addr, int len )
{
#if DEBUG
    int i;
    unsigned char buf[17];
    unsigned char *p;
    char          out[80];
    int           o;
    int           save_errno;

    if ( g_running_level < PROXY_LOG_DEBUG ) {
        return;
    }
    if ( len > 0 ) {
        save_errno = errno;
        if ( desc ) {
            PRINT("%s\n", desc);
        }
        for ( p = addr, i = 0, o = 0; i < len; ++i ) {
            if ( !(i % 16) ) {
                if ( i ) {
                    sprintf( out + o, "  %s\n", buf );
                    PRINT("%s", out);
                    o = 0;
                }
                sprintf( out + o, "  %08x ", (int)(long)addr + i );
                o = strlen( out );
            }
            sprintf( out + o, "%02x", p[i] );
            o += 2;
            if ( p[i] < 0x20 || p[i] > 0x7e ) {
                buf[i % 16] = '.';
            } else {
                buf[i % 16] = p[i];
            }
            buf[(i % 16) + 1] = '\0';
        }
        while ( (i++ % 16) ) {
            sprintf( out + o, "  " );
            o += 2;
        }
        sprintf( out + o, "  %s\n", buf );
        PRINT("%s", out);
        errno = save_errno;
    }
#endif
}


