/* Receive one selected TRDP PD ComID on the loopback interface. */

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "pd_communication.h"
#include "vos_thread.h"
#include "vos_utils.h"

#define LOOPBACK_IP "127.0.0.1"

static volatile sig_atomic_t keep_running = 1;

static void stop (int signal_number)
{
    (void) signal_number;
    keep_running = 0;
}

static void usage (const char *program)
{
    fprintf(stderr, "Usage: %s COMID\n", program);
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

static void print_payload (const UINT8 *payload, UINT32 size)
{
    UINT32 index;

    printf("hex=");
    for (index = 0u; index < size; ++index)
    {
        printf("%02X%s", payload[index], index + 1u == size ? "" : " ");
    }
    printf("  text=\"");
    for (index = 0u; index < size; ++index)
    {
        putchar(isprint((unsigned char) payload[index]) ? payload[index] : '.');
    }
    printf("\"\n");
}

int main (int argc, char *argv[])
{
    PD_COMM_CONFIG_T config;
    PD_COMM_T communication;
    TRDP_PD_INFO_T info;
    UINT8 payload[TRDP_MAX_PD_DATA_SIZE];
    UINT32 size;
    UINT32 comid;
    TRDP_ERR_T error;

    if (argc != 2 || !parse_comid(argv[1], &comid))
    {
        usage(argv[0]);
        return 1;
    }

    pd_comm_config_init(&config);
    pd_comm_config_set_comid(&config, comid);
    if (pd_comm_config_set_source_ip(&config, LOOPBACK_IP) != TRDP_NO_ERR)
    {
        fprintf(stderr, "Cannot configure loopback address.\n");
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
    printf("Listening on %s for PD ComID %u; press Ctrl-C to stop.\n", LOOPBACK_IP, comid);
    while (keep_running)
    {
        error = pd_comm_process(&communication);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }

        size = sizeof(payload);
        error = pd_comm_receive(&communication, &info, payload, &size);
        if (error == TRDP_NO_ERR)
        {
            printf("ComID %u from %s, %u bytes: ", info.comId, vos_ipDotted(info.srcIpAddr), size);
            print_payload(payload, size);
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
