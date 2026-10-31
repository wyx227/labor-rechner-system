/* Simple PD publisher built solely on pd_communication.h. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pd_communication.h"
#include "vos_thread.h"
#include "vos_utils.h"

#define DEFAULT_COMID 1000u

static volatile sig_atomic_t keep_running = 1;

static void stop (int signal_number)
{
    (void) signal_number;
    keep_running = 0;
}

static void usage (const char *program)
{
    fprintf(stderr, "Usage: %s DESTINATION_IP [SOURCE_IP] [COMID]\n", program);
}

static int parse_comid (const char *text, UINT32 *comid)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);

    if (*text == '\0' || *end != '\0' || value > 0xffffffffUL)
    {
        return 0;
    }
    *comid = (UINT32) value;
    return 1;
}

int main (int argc, char *argv[])
{
    PD_COMM_CONFIG_T config;
    PD_COMM_T communication;
    UINT8 payload[64];
    UINT32 comid = DEFAULT_COMID;
    UINT32 counter = 0u;
    TRDP_ERR_T error;

    if (argc < 2 || argc > 4)
    {
        usage(argv[0]);
        return 1;
    }
    if (argc == 4 && !parse_comid(argv[3], &comid))
    {
        usage(argv[0]);
        return 1;
    }

    pd_comm_config_init(&config);
    pd_comm_config_set_comid(&config, comid);
    if (pd_comm_config_set_destination_ip(&config, argv[1]) != TRDP_NO_ERR ||
        (argc >= 3 && pd_comm_config_set_source_ip(&config, argv[2]) != TRDP_NO_ERR))
    {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    error = pd_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        (void) snprintf((char *) payload, sizeof(payload), "PD wrapper sender");
        error = pd_comm_open_publisher(&communication, payload, (UINT32) strlen((char *) payload));
    }
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "PD sender setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        pd_comm_close(&communication);
        return 1;
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    printf("Sending PD ComID %u to %s; press Ctrl-C to stop.\n", comid, argv[1]);
    while (keep_running)
    {
        (void) snprintf((char *) payload, sizeof(payload), "PD wrapper message %u", counter++);
        error = pd_comm_send(&communication, payload, (UINT32) strlen((char *) payload));
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD send failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
        error = pd_comm_process(&communication);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
        (void) vos_threadDelay(100u);
    }

    pd_comm_close(&communication);
    return error == TRDP_NO_ERR ? 0 : 1;
}
