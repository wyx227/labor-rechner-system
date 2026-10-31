/* Minimal TRDP MR/MP request-and-reply example using md_communication.h. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md_communication.h"
#include "vos_utils.h"

#define DEFAULT_COMID 1000u

typedef struct
{
    int completed;
    int failed;
} REQUEST_STATE_T;

static volatile sig_atomic_t keep_running = 1;

static void stop (int signal_number)
{
    (void) signal_number;
    keep_running = 0;
}

static void usage (const char *program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s request DESTINATION_IP [SOURCE_IP] [COMID]\n"
            "  %s reply [SOURCE_IP] [COMID]\n",
            program, program);
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

static void print_payload (const UINT8 *data, UINT32 size)
{
    if (data != NULL && size > 0u)
    {
        printf("%.*s", (int) size, (const char *) data);
    }
}

static void on_mp (void *user_ref,
                   TRDP_APP_SESSION_T session,
                   const TRDP_MD_INFO_T *message,
                   UINT8 *data,
                   UINT32 size)
{
    REQUEST_STATE_T *state = (REQUEST_STATE_T *) user_ref;

    (void) session;
    if (message->resultCode == TRDP_NO_ERR && message->msgType == TRDP_MSG_MP)
    {
        printf("Received MP from %s: ", vos_ipDotted(message->srcIpAddr));
        print_payload(data, size);
        printf("\n");
        state->completed = 1;
    }
    else
    {
        fprintf(stderr, "MR session ended with TRDP error %d.\n", message->resultCode);
        state->failed = 1;
    }
}

static void on_mr (void *user_ref,
                   MD_COMM_T *communication,
                   const TRDP_MD_INFO_T *request,
                   UINT8 *data,
                   UINT32 size)
{
    static const UINT8 reply[] = "Hello from MD MP wrapper";
    TRDP_ERR_T error;

    (void) user_ref;
    if (request->resultCode != TRDP_NO_ERR)
    {
        return;
    }
    printf("Received MR from %s: ", vos_ipDotted(request->srcIpAddr));
    print_payload(data, size);
    printf("\n");

    error = md_comm_reply(communication, request, 0u, reply, (UINT32) (sizeof(reply) - 1u));
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "MP reply failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
    }
}

static int run_requester (int argc, char *argv[])
{
    static const UINT8 request[] = "Hello from MD MR wrapper";
    MD_COMM_CONFIG_T config;
    MD_COMM_T communication;
    REQUEST_STATE_T state = {0, 0};
    TRDP_UUID_T session_id;
    UINT32 comid = DEFAULT_COMID;
    TRDP_ERR_T error;

    if (argc < 3 || argc > 5 || (argc == 5 && !parse_comid(argv[4], &comid)))
    {
        return -1;
    }
    md_comm_config_init(&config);
    md_comm_config_set_comid(&config, comid);
    if (md_comm_config_set_destination_ip(&config, argv[2]) != TRDP_NO_ERR ||
        (argc >= 4 && md_comm_config_set_source_ip(&config, argv[3]) != TRDP_NO_ERR))
    {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    error = md_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = md_comm_send_mr(&communication, request, (UINT32) (sizeof(request) - 1u),
                                1u, on_mp, &state, &session_id);
    }
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "MR request setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        md_comm_close(&communication);
        return 1;
    }

    printf("Sent MR: ComID %u to %s; waiting for MP.\n", comid, argv[2]);
    while (state.completed == 0 && state.failed == 0)
    {
        error = md_comm_process(&communication);
        if (error != TRDP_NO_ERR && error != TRDP_BLOCK_ERR)
        {
            fprintf(stderr, "MD process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            state.failed = 1;
        }
    }

    md_comm_close(&communication);
    return state.failed == 0 ? 0 : 1;
}

static int run_replier (int argc, char *argv[])
{
    MD_COMM_CONFIG_T config;
    MD_COMM_T communication;
    UINT32 comid = DEFAULT_COMID;
    TRDP_ERR_T error;

    if (argc > 4 || (argc == 4 && !parse_comid(argv[3], &comid)))
    {
        return -1;
    }
    md_comm_config_init(&config);
    md_comm_config_set_comid(&config, comid);
    if (argc >= 3 && md_comm_config_set_source_ip(&config, argv[2]) != TRDP_NO_ERR)
    {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    error = md_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = md_comm_listen(&communication, NULL, on_mr, NULL);
    }
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "MR listener setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        md_comm_close(&communication);
        return 1;
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    printf("Listening for MR on ComID %u; press Ctrl-C to stop.\n", comid);
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

int main (int argc, char *argv[])
{
    int result;

    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "request") == 0)
    {
        result = run_requester(argc, argv);
    }
    else if (strcmp(argv[1], "reply") == 0)
    {
        result = run_replier(argc, argv);
    }
    else
    {
        usage(argv[0]);
        return 1;
    }

    if (result == -1)
    {
        usage(argv[0]);
        return 1;
    }
    return result;
}
