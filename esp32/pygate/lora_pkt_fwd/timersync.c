/*
 * Copyright (c) 2021, Pycom Limited.
 *
 * This software is licensed under the GNU GPL version 3 or any
 * later version, with permitted additional terms. For more information
 * see the Pycom Licence v1.0 document supplied with this file, or
 * available at https://www.pycom.io/opensource/licensing
 */
/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
    LoRa concentrator : Timer synchronization
        Provides synchronization between unix, concentrator and gps clocks

License: Revised BSD License, see LICENSE.TXT file include in the project
Maintainer: Michael Coracin
*/

/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

#include <stdio.h>        /* printf, fprintf, snprintf, fopen, fputs */
#include <stdint.h>        /* C99 types */
#include <pthread.h>

#include "trace.h"
#include "timersync.h"
#include "loragw_hal.h"
#include "loragw_reg.h"
#include "loragw_aux.h"

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS & TYPES -------------------------------------------- */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define timersub(a, b, result)                                                \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)

/* -------------------------------------------------------------------------- */
/* --- PRIVATE VARIABLES (GLOBAL) ------------------------------------------- */

static pthread_mutex_t mx_timersync = PTHREAD_MUTEX_INITIALIZER; /* control access to timer sync offsets */
static struct timeval offset_unix_concent = {0, 0}; /* timer offset between unix host and concentrator */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE SHARED VARIABLES (GLOBAL) ------------------------------------ */
extern bool exit_sig;
extern bool quit_sig;
extern pthread_mutex_t mx_concent;

/* -------------------------------------------------------------------------- */
/* --- PUBLIC FUNCTIONS DEFINITION ------------------------------------------ */

int get_concentrator_time(struct timeval *concent_time, struct timeval unix_time) {
    struct timeval local_timeval;

    if (concent_time == NULL) {
        MSG_ERROR("[ts  ] %s invalid parameter\n", __FUNCTION__);
        return -1;
    }

    pthread_mutex_lock(&mx_timersync); /* protect global variable access */
    timersub(&unix_time, &offset_unix_concent, &local_timeval);
    pthread_mutex_unlock(&mx_timersync);

    /* TODO: handle sx1301 coutner wrap-up !! */
    concent_time->tv_sec = local_timeval.tv_sec;
    concent_time->tv_usec = local_timeval.tv_usec;
    /*
    MSG_DEBUG("[ts  ] --> TIME: unix current time is    (%ld,%ld)[s,us]\n", unix_time.tv_sec, unix_time.tv_usec);
    MSG_DEBUG("[ts  ]           concent current time is (%ld,%ld)[s,us]\n", local_timeval.tv_sec, local_timeval.tv_usec);
    MSG_DEBUG("[ts  ]           offset is               (%ld,%ld)[s,us]\n", offset_unix_concent.tv_sec, offset_unix_concent.tv_usec);
    */
    return 0;
}

/* ---------------------------------------------------------------------------------------------- */
/* --- THREAD 6: REGULARLAY MONITOR THE OFFSET BETWEEN UNIX CLOCK AND CONCENTRATOR CLOCK -------- */

void thread_timersync(void) {
    MSG_INFO("[ts  ] start\n");
    struct timeval unix_timeval;
    struct timeval concentrator_timeval;
    uint32_t sx1301_timecount = 0;
    struct timeval offset_previous = {0, 0};
    struct timeval offset_drift = {0, 0}; /* delta between current and previous offset */

    while (!exit_sig && !quit_sig) {
        /* Get current unix time */
        gettimeofday(&unix_timeval, NULL);

        /* Get current concentrator counter value (1MHz) */
        lgw_get_trigcnt(&sx1301_timecount);

        concentrator_timeval.tv_sec = sx1301_timecount / 1000000UL;
        concentrator_timeval.tv_usec = sx1301_timecount - (concentrator_timeval.tv_sec * 1000000UL);

        /* Compute offset between unix and concentrator timers, with microsecond precision */
        offset_previous.tv_sec = offset_unix_concent.tv_sec;
        offset_previous.tv_usec = offset_unix_concent.tv_usec;

        pthread_mutex_lock(&mx_timersync); /* protect global variable access */
        timersub(&unix_timeval, &concentrator_timeval, &offset_unix_concent);
        pthread_mutex_unlock(&mx_timersync);

        timersub(&offset_unix_concent, &offset_previous, &offset_drift);

/*
        MSG_DEBUG("[ts  ] unix_timeval (%ld,%ld)[s,us]\n",
                  unix_timeval.tv_sec,
                  unix_timeval.tv_usec);
        MSG_DEBUG("[ts  ] concentrator (%ld,%ld)[s,us] = %u [us]\n",
                  concentrator_timeval.tv_sec,
                  concentrator_timeval.tv_usec,
                  sx1301_timecount);

        MSG_DEBUG("[ts  ] offset       (%ld,%ld)[s,us], drift=%ld [us]\n",
            offset_unix_concent.tv_sec,
            offset_unix_concent.tv_usec,
            offset_drift.tv_sec * 1000000UL + offset_drift.tv_usec);
*/

        /* delay next sync */
        /* If we consider a crystal oscillator precision of about 20ppm worst case, and a clock
            running at 1MHz, this would mean 1µs drift every 50000µs (10000000/20).
            As here the time precision is not critical, we should be able to cope with at least 1ms drift,
            which should occur after 50s (50000µs * 1000).
            Let's set the thread sleep to 1 minute for now */
        wait_ms(750);
    }
    MSG_INFO("[ts  ] end exit=%u quit=%u\n", exit_sig, quit_sig);
}
