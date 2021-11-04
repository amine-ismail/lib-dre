/*

    Author:     Amine Ismail
    email: Amine.Ismail@gmail.com
*/

#ifndef PROXY_LOG_H
#define PROXY_LOG_H

#include <stdint.h>
#include <syslog.h>
#include <time.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROXY_LOG_ERROR     LOG_ERR
#define PROXY_LOG_WARNING   LOG_WARNING
#define PROXY_LOG_INFO      LOG_NOTICE
#define PROXY_LOG_DEBUG     LOG_DEBUG

#if DEBUG
#define DBG(format, ...) logDebug(format, ##__VA_ARGS__)
#else
#define DBG(format, ...)
#endif
#define INFO(format, ...) logInfo(format, ##__VA_ARGS__)
#define WARN(format, ...) logWarning("%s, "format, __func__, ##__VA_ARGS__)
#define ERR(format, ...) logError("### %s, "format, __func__, ##__VA_ARGS__)

typedef struct tm *(f_localtime)(const time_t *clock, struct tm *result);


/*----------------------------------------------------------------------------*
 set_logger_level
 *----------------------------------------------------------------------------*/
void set_logger_level( int level );


/*----------------------------------------------------------------------------*
 get_logger_level
 *----------------------------------------------------------------------------*/
int get_logger_level();


/*----------------------------------------------------------------------------*
 set_logger_id: for forked processes, add an id to the logs
 *----------------------------------------------------------------------------*/
void set_logger_id( int id );


/*----------------------------------------------------------------------------*
 set_logger_localtime:
 *----------------------------------------------------------------------------*/
void set_logger_localtime( f_localtime *func );


/*----------------------------------------------------------------------------*
 set_logger_log_file:
 *----------------------------------------------------------------------------*/
void set_logger_log_file( FILE *file );


/*----------------------------------------------------------------------------*
 set_logger_syslog: use syslog instead of stderr
 *----------------------------------------------------------------------------*/
void set_logger_syslog( int syslog );


/*----------------------------------------------------------------------------*
 get_logger_syslog
 *----------------------------------------------------------------------------*/
int get_logger_syslog();


/*----------------------------------------------------------------------------*
 logger
 *----------------------------------------------------------------------------*/
int logger( uint8_t level, const char * format, ... )
#if defined(__GNUC__)
    __attribute__ ((format(printf, 2, 3)))
#endif
    ;

/*----------------------------------------------------------------------------*
 logDebug
 *----------------------------------------------------------------------------*/
#ifdef DEBUG
#define logDebug(arg...) logger(PROXY_LOG_DEBUG, arg)
#else
#define logDebug(arg...)
#endif


/*----------------------------------------------------------------------------*
 logInfo
 *----------------------------------------------------------------------------*/
#define logInfo(arg...) logger(PROXY_LOG_INFO, arg)


/*----------------------------------------------------------------------------*
 logWarning
 *----------------------------------------------------------------------------*/
#define logWarning(arg...) logger(PROXY_LOG_WARNING, arg)


/*----------------------------------------------------------------------------*
 logError
 *----------------------------------------------------------------------------*/
#define logError(arg...) logger(PROXY_LOG_ERROR, arg)


/*----------------------------------------------------------------------------*
 logHexDump (debug only)
 *----------------------------------------------------------------------------*/
void logHexDump( char *desc, void *addr, int len );


#ifdef __cplusplus
}
#endif

#endif
