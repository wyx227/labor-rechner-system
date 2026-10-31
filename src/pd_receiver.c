/* Simple PD subscriber built solely on pd_communication.h. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

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
    fprintf(stderr, "Usage: %s [SOURCE_IP] [MULTICAST_DESTINATION_IP] [COMID]\n", program);
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
    TRDP_PD_INFO_T info;
    UINT8 payload[TRDP_MAX_PD_DATA_SIZE + 1u];
    UINT32 size;
    UINT32 comid = DEFAULT_COMID;
    TRDP_ERR_T error;

    if (argc > 4)
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
    if ((argc >= 2 && pd_comm_config_set_source_ip(&config, argv[1]) != TRDP_NO_ERR) ||
        (argc >= 3 && pd_comm_config_set_destination_ip(&config, argv[2]) != TRDP_NO_ERR))
    {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    error = pd_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = pd_comm_open_subscriber(&communication);
    }
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "PD receiver setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        pd_comm_close(&communication);
        return 1;
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    printf("Receiving PD ComID %u; press Ctrl-C to stop.\n", comid);
    while (keep_running)
    {
        error = pd_comm_process(&communication);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }

        size = TRDP_MAX_PD_DATA_SIZE;
        error = pd_comm_receive(&communication, &info, payload, &size);
        if (error == TRDP_NO_ERR)
        {
            payload[size] = '\0';
            printf("ComID %u from %s: %u bytes: %s\n",
                   info.comId, vos_ipDotted(info.srcIpAddr), size, payload);
        }
        else if (error != TRDP_NODATA_ERR && error != TRDP_TIMEOUT_ERR)
        {
            fprintf(stderr, "PD receive failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
        (void) vos_threadDelay(10u);
    }

    pd_comm_close(&communication);
    return (error == TRDP_NO_ERR || error == TRDP_NODATA_ERR || error == TRDP_TIMEOUT_ERR) ? 0 : 1;
}
