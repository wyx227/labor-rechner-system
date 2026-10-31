/* Receive and print TRDP MD notifications (MN) through md_communication.h. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "md_communication.h"
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
    fprintf(stderr,
            "Usage: %s [SOURCE_IP] [MULTICAST_DESTINATION_IP] [COMID]\n",
            program);
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

static void on_mn (void *user_ref,
                   MD_COMM_T *communication,
                   const TRDP_MD_INFO_T *message,
                   UINT8 *data,
                   UINT32 size)
{
    (void) user_ref;
    (void) communication;

    if (message->resultCode != TRDP_NO_ERR)
    {
        fprintf(stderr, "MN callback reported TRDP error %d.\n", message->resultCode);
        return;
    }

    printf("Received MN: ComID %u from %s, %u bytes: ",
           message->comId, vos_ipDotted(message->srcIpAddr), size);
    if (data != NULL && size > 0u)
    {
        printf("%.*s", (int) size, (const char *) data);
    }
    printf("\n");
}

int main (int argc, char *argv[])
{
    MD_COMM_CONFIG_T config;
    MD_COMM_T communication;
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

    md_comm_config_init(&config);
    md_comm_config_set_comid(&config, comid);
    if ((argc >= 2 && md_comm_config_set_source_ip(&config, argv[1]) != TRDP_NO_ERR) ||
        (argc >= 3 && md_comm_config_set_destination_ip(&config, argv[2]) != TRDP_NO_ERR))
    {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    error = md_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = md_comm_listen(&communication, on_mn, NULL, NULL);
    }
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "MN listener setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        md_comm_close(&communication);
        return 1;
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    printf("Listening for MN on ComID %u; press Ctrl-C to stop.\n", comid);
    while (keep_running)
    {
        error = md_comm_process(&communication);
        if (error != TRDP_NO_ERR && error != TRDP_BLOCK_ERR)
        {
            fprintf(stderr, "MD process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
    }

    md_comm_close(&communication);
    return (error == TRDP_NO_ERR || error == TRDP_BLOCK_ERR) ? 0 : 1;
}
