/* Send one TRDP MD notification (MN) through md_communication.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md_communication.h"
#include "vos_utils.h"

#define DEFAULT_COMID 1000u

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
    static const UINT8 message[] = "Hello from MD notification wrapper";
    MD_COMM_CONFIG_T config;
    MD_COMM_T communication;
    UINT32 comid = DEFAULT_COMID;
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

    md_comm_config_init(&config);
    md_comm_config_set_comid(&config, comid);
    if (md_comm_config_set_destination_ip(&config, argv[1]) != TRDP_NO_ERR ||
        (argc >= 3 && md_comm_config_set_source_ip(&config, argv[2]) != TRDP_NO_ERR))
    {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    error = md_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = md_comm_send_mn(&communication, message, (UINT32) (sizeof(message) - 1u), NULL, NULL);
    }
    if (error == TRDP_NO_ERR)
    {
        /* Flush the outgoing MD queue once. */
        error = md_comm_process(&communication);
    }
    if (error != TRDP_NO_ERR && error != TRDP_BLOCK_ERR)
    {
        fprintf(stderr, "MD notification failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        md_comm_close(&communication);
        return 1;
    }

    printf("Sent MN: ComID %u to %s.\n", comid, argv[1]);
    md_comm_close(&communication);
    return 0;
}
