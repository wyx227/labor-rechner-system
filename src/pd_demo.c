/*
 * pd-demo: publish randomized printable-character PD telegrams.
 *
 * The program is deliberately small, so it can serve as a starting point for
 * applications in this project.  TRDP's process loop determines when a
 * telegram is sent; the payload is refreshed once per loop iteration.
 */

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "trdp_if_light.h"
#include "vos_thread.h"
#include "vos_utils.h"

#define DEFAULT_COMID       1000u
#define DEFAULT_CYCLE_US    1000000u
#define RESERVED_MEMORY     160000u

static volatile sig_atomic_t keep_running = 1;

static void stop (int signal_number)
{
    (void) signal_number;
    keep_running = 0;
}

static void usage (const char *program)
{
    printf("Usage: %s --destination ADDRESS [options]\n", program);
    printf("Send cyclic TRDP PD telegrams with randomized printable payloads.\n\n");
    printf("  -d, --destination ADDRESS   Destination IPv4 address (required)\n");
    printf("  -c, --comid NUMBER          ComID (default: %u)\n", DEFAULT_COMID);
    printf("  -t, --cycle-us MICROSECONDS Cycle time in microseconds (default: %u)\n", DEFAULT_CYCLE_US);
    printf("  -s, --payload-size BYTES    Payload size: 0-%u (default: 64)\n", TRDP_MAX_PD_DATA_SIZE);
    printf("  -i, --source ADDRESS        Source/interface IPv4 address (default: automatic)\n");
    printf("  -v, --verbose               Print a status line per sent update\n");
    printf("  -h, --help                  Show this help text\n");
}

static int parse_u32 (const char *text, UINT32 *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);

    if (*text == '\0' || *end != '\0' || parsed > 0xffffffffUL)
    {
        return 0;
    }
    *value = (UINT32) parsed;
    return 1;
}

static void randomize_payload (UINT8 *payload, UINT32 size)
{
    UINT32 i;

    for (i = 0u; i < size; ++i)
    {
        payload[i] = (UINT8) (' ' + (rand() % 95));
    }
}

int main (int argc, char *argv[])
{
    static const struct option options[] = {
        {"destination", required_argument, NULL, 'd'},
        {"comid", required_argument, NULL, 'c'},
        {"cycle-us", required_argument, NULL, 't'},
        {"payload-size", required_argument, NULL, 's'},
        {"source", required_argument, NULL, 'i'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    TRDP_MEM_CONFIG_T memory_config = {NULL, RESERVED_MEMORY, {0u}};
    TRDP_PD_CONFIG_T pd_config = {NULL, NULL, TRDP_PD_DEFAULT_SEND_PARAM,
                                  TRDP_FLAGS_NONE, 1000000u, TRDP_TO_SET_TO_ZERO, 0u};
    TRDP_PROCESS_CONFIG_T process_config = {"pd-demo", "", "",
                                            TRDP_PROCESS_DEFAULT_CYCLE_TIME, 0u,
                                            TRDP_OPTION_BLOCK, 0u};
    TRDP_APP_SESSION_T session;
    TRDP_PUB_T publisher;
    UINT8 payload[TRDP_MAX_PD_DATA_SIZE];
    UINT32 comid = DEFAULT_COMID;
    UINT32 cycle_us = DEFAULT_CYCLE_US;
    UINT32 payload_size = 64u;
    UINT32 source_ip = 0u;
    UINT32 destination_ip = 0u;
    int verbose = 0;
    int option;
    TRDP_ERR_T error;

    while ((option = getopt_long(argc, argv, "d:c:t:s:i:vh", options, NULL)) != -1)
    {
        switch (option)
        {
            case 'd': destination_ip = vos_dottedIP(optarg); break;
            case 'i': source_ip = vos_dottedIP(optarg); break;
            case 'c': if (!parse_u32(optarg, &comid)) goto invalid_argument; break;
            case 't': if (!parse_u32(optarg, &cycle_us) || cycle_us == 0u) goto invalid_argument; break;
            case 's': if (!parse_u32(optarg, &payload_size) || payload_size > TRDP_MAX_PD_DATA_SIZE) goto invalid_argument; break;
            case 'v': verbose = 1; break;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 1;
        }
    }

    if (destination_ip == 0u)
    {
        fprintf(stderr, "A valid --destination IPv4 address is required.\n");
        usage(argv[0]);
        return 1;
    }

    srand((unsigned int) (time(NULL) ^ (time_t) getpid()));
    randomize_payload(payload, payload_size);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    error = tlc_init(NULL, NULL, &memory_config);
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "TRDP initialization failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        return 1;
    }
    error = tlc_openSession(&session, source_ip, 0u, NULL, &pd_config, NULL, &process_config);
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "TRDP session initialization failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        tlc_terminate();
        return 1;
    }
    error = tlp_publish(session, &publisher, NULL, NULL, 0u, comid, 0u, 0u,
                        source_ip, destination_ip, cycle_us, 0u, TRDP_FLAGS_NONE,
                        payload, payload_size);
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "PD publish setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        tlc_closeSession(session);
        tlc_terminate();
        return 1;
    }
    error = tlc_updateSession(session);
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "TRDP session update failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        tlp_unpublish(session, publisher);
        tlc_closeSession(session);
        tlc_terminate();
        return 1;
    }

    printf("Sending PD: ComID %u, destination %s, cycle %u us, payload %u bytes.\n",
           comid, vos_ipDotted(destination_ip), cycle_us, payload_size);
    while (keep_running)
    {
        TRDP_TIME_T wait_time;

        (void) tlc_getInterval(session, &wait_time, NULL, NULL);
        (void) vos_threadDelay((UINT32) wait_time.tv_usec / 1000u);
        error = tlc_process(session, NULL, NULL);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "TRDP process error: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
        randomize_payload(payload, payload_size);
        error = tlp_put(session, publisher, payload, payload_size);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD update failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
        if (verbose)
        {
            printf("Updated %u-byte payload.\n", payload_size);
        }
    }

    tlp_unpublish(session, publisher);
    tlc_closeSession(session);
    tlc_terminate();
    return error == TRDP_NO_ERR ? 0 : 1;

invalid_argument:
    fprintf(stderr, "Invalid value for option '%s'.\n", optarg);
    usage(argv[0]);
    return 1;
}
