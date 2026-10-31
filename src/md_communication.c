#include <string.h>

#include "md_communication.h"
#include "vos_utils.h"

#define MD_COMM_RESERVED_MEMORY 160000u

static TRDP_ERR_T md_comm_require_open (const MD_COMM_T *communication)
{
    if (communication == NULL || communication->initialized == FALSE)
    {
        return TRDP_PARAM_ERR;
    }
    return TRDP_NO_ERR;
}

static TRDP_FLAGS_T md_comm_flags (const MD_COMM_T *communication)
{
    return communication->config.use_udp == TRUE
        ? TRDP_FLAGS_CALLBACK
        : (TRDP_FLAGS_CALLBACK | TRDP_FLAGS_TCP);
}

static void md_comm_listener_callback (void *reference,
                                       TRDP_APP_SESSION_T session,
                                       const TRDP_MD_INFO_T *message,
                                       UINT8 *data,
                                       UINT32 size)
{
    MD_COMM_T *communication = (MD_COMM_T *) reference;

    (void) session;
    if (communication == NULL || message == NULL)
    {
        return;
    }

    if (message->msgType == TRDP_MSG_MN && communication->mn_callback != NULL)
    {
        communication->mn_callback(communication->listener_user_ref, communication, message, data, size);
    }
    else if (message->msgType == TRDP_MSG_MR && communication->mr_callback != NULL)
    {
        communication->mr_callback(communication->listener_user_ref, communication, message, data, size);
    }
}

void md_comm_config_init (MD_COMM_CONFIG_T *config)
{
    if (config == NULL)
    {
        return;
    }

    config->comid = 0u;
    config->use_udp = TRUE;
    config->destination_ip = 0u;
    config->source_ip = 0u;
    config->reply_timeout_us = MD_COMM_DEFAULT_REPLY_TIMEOUT_US;
    config->retries = MD_COMM_DEFAULT_RETRIES;
}

void md_comm_config_set_comid (MD_COMM_CONFIG_T *config, UINT32 comid)
{
    if (config != NULL)
    {
        config->comid = comid;
    }
}

void md_comm_config_set_udp (MD_COMM_CONFIG_T *config, BOOL8 use_udp)
{
    if (config != NULL)
    {
        config->use_udp = use_udp == FALSE ? FALSE : TRUE;
    }
}

static TRDP_ERR_T md_comm_set_ip (TRDP_IP_ADDR_T *target, const CHAR8 *address)
{
    TRDP_IP_ADDR_T parsed;

    if (target == NULL || address == NULL || *address == '\0')
    {
        return TRDP_PARAM_ERR;
    }
    parsed = vos_dottedIP(address);
    if (parsed == 0u && strcmp(address, "0.0.0.0") != 0)
    {
        return TRDP_PARAM_ERR;
    }
    *target = parsed;
    return TRDP_NO_ERR;
}

TRDP_ERR_T md_comm_config_set_destination_ip (MD_COMM_CONFIG_T *config, const CHAR8 *address)
{
    return config == NULL ? TRDP_PARAM_ERR : md_comm_set_ip(&config->destination_ip, address);
}

TRDP_ERR_T md_comm_config_set_source_ip (MD_COMM_CONFIG_T *config, const CHAR8 *address)
{
    return config == NULL ? TRDP_PARAM_ERR : md_comm_set_ip(&config->source_ip, address);
}

TRDP_ERR_T md_comm_open (MD_COMM_T *communication, const MD_COMM_CONFIG_T *config)
{
    TRDP_MEM_CONFIG_T memory_config = {NULL, MD_COMM_RESERVED_MEMORY, {0u}};
    TRDP_MD_CONFIG_T md_config = {NULL, NULL, TRDP_MD_DEFAULT_SEND_PARAM,
                                  TRDP_FLAGS_CALLBACK, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    TRDP_PROCESS_CONFIG_T process_config = {"md-comm", "", "",
                                            TRDP_PROCESS_DEFAULT_CYCLE_TIME, 0u,
                                            TRDP_OPTION_BLOCK, 0u};
    TRDP_ERR_T error;

    if (communication == NULL || config == NULL)
    {
        return TRDP_PARAM_ERR;
    }

    memset(communication, 0, sizeof(*communication));
    md_config.flags = config->use_udp == TRUE ? TRDP_FLAGS_CALLBACK
                                               : (TRDP_FLAGS_CALLBACK | TRDP_FLAGS_TCP);
    error = tlc_init(NULL, NULL, &memory_config);
    if (error != TRDP_NO_ERR)
    {
        return error;
    }
    error = tlc_openSession(&communication->session, config->source_ip, 0u,
                            NULL, NULL, &md_config, &process_config);
    if (error != TRDP_NO_ERR)
    {
        tlc_terminate();
        return error;
    }

    communication->config = *config;
    communication->initialized = TRUE;
    return TRDP_NO_ERR;
}

TRDP_ERR_T md_comm_send_mn (MD_COMM_T *communication,
                            const UINT8 *data,
                            UINT32 size,
                            TRDP_MD_CALLBACK_T callback,
                            void *user_ref)
{
    if (communication == NULL)
    {
        return TRDP_PARAM_ERR;
    }
    return md_comm_send_mn_with_comid(communication, communication->config.comid,
                                      data, size, callback, user_ref);
}

TRDP_ERR_T md_comm_send_mn_with_comid (MD_COMM_T *communication,
                                       UINT32 comid,
                                       const UINT8 *data,
                                       UINT32 size,
                                       TRDP_MD_CALLBACK_T callback,
                                       void *user_ref)
{
    if (md_comm_require_open(communication) != TRDP_NO_ERR ||
        communication->config.destination_ip == 0u)
    {
        return TRDP_PARAM_ERR;
    }
    return tlm_notify(communication->session, user_ref, callback,
                      comid, 0u, 0u,
                      communication->config.source_ip, communication->config.destination_ip,
                      md_comm_flags(communication), data, size, NULL, NULL);
}

TRDP_ERR_T md_comm_send_mr (MD_COMM_T *communication,
                            const UINT8 *data,
                            UINT32 size,
                            UINT32 expected_replies,
                            TRDP_MD_CALLBACK_T callback,
                            void *user_ref,
                            TRDP_UUID_T *session_id)
{
    if (md_comm_require_open(communication) != TRDP_NO_ERR ||
        communication->config.destination_ip == 0u || callback == NULL || session_id == NULL)
    {
        return TRDP_PARAM_ERR;
    }
    return tlm_request(communication->session, user_ref, callback, session_id,
                       communication->config.comid, 0u, 0u,
                       communication->config.source_ip, communication->config.destination_ip,
                       md_comm_flags(communication), expected_replies,
                       communication->config.reply_timeout_us, communication->config.retries,
                       data, size, NULL, NULL);
}

TRDP_ERR_T md_comm_abort_mr (MD_COMM_T *communication, const TRDP_UUID_T *session_id)
{
    if (md_comm_require_open(communication) != TRDP_NO_ERR || session_id == NULL)
    {
        return TRDP_PARAM_ERR;
    }
    return tlm_abortSession(communication->session, session_id);
}

TRDP_ERR_T md_comm_listen (MD_COMM_T *communication,
                           MD_COMM_MN_CALLBACK_T mn_callback,
                           MD_COMM_MR_CALLBACK_T mr_callback,
                           void *user_ref)
{
    TRDP_ERR_T error;

    if (md_comm_require_open(communication) != TRDP_NO_ERR || communication->listener_open == TRUE ||
        (mn_callback == NULL && mr_callback == NULL))
    {
        return TRDP_PARAM_ERR;
    }

    communication->mn_callback = mn_callback;
    communication->mr_callback = mr_callback;
    communication->listener_user_ref = user_ref;
    error = tlm_addListener(communication->session, &communication->listener,
                            communication, md_comm_listener_callback, TRUE,
                            communication->config.comid, 0u, 0u,
                            communication->config.source_ip, VOS_INADDR_ANY,
                            communication->config.destination_ip, md_comm_flags(communication),
                            NULL, NULL);
    if (error != TRDP_NO_ERR)
    {
        communication->mn_callback = NULL;
        communication->mr_callback = NULL;
        communication->listener_user_ref = NULL;
        return error;
    }
    communication->listener_open = TRUE;
    return TRDP_NO_ERR;
}

TRDP_ERR_T md_comm_reply (MD_COMM_T *communication,
                          const TRDP_MD_INFO_T *request,
                          UINT32 user_status,
                          const UINT8 *data,
                          UINT32 size)
{
    if (md_comm_require_open(communication) != TRDP_NO_ERR || request == NULL ||
        request->msgType != TRDP_MSG_MR)
    {
        return TRDP_PARAM_ERR;
    }
    return tlm_reply(communication->session, &request->sessionId,
                     communication->config.comid, user_status, data, size, NULL);
}

TRDP_ERR_T md_comm_process (MD_COMM_T *communication)
{
    TRDP_FDS_T file_descriptors;
    TRDP_TIME_T interval;
    TRDP_SOCK_T no_descriptors;
    INT32 ready_descriptors;
    TRDP_ERR_T error;

    if (md_comm_require_open(communication) != TRDP_NO_ERR)
    {
        return TRDP_PARAM_ERR;
    }

    FD_ZERO(&file_descriptors);
    error = tlm_getInterval(communication->session, &interval, &file_descriptors, &no_descriptors);
    if (error != TRDP_NO_ERR)
    {
        return error;
    }
    ready_descriptors = vos_select(no_descriptors, &file_descriptors, NULL, NULL, &interval);
    return tlm_process(communication->session, &file_descriptors, &ready_descriptors);
}

void md_comm_close (MD_COMM_T *communication)
{
    if (communication == NULL || communication->initialized == FALSE)
    {
        return;
    }
    if (communication->listener_open == TRUE)
    {
        (void) tlm_delListener(communication->session, communication->listener);
    }
    (void) tlc_closeSession(communication->session);
    tlc_terminate();
    memset(communication, 0, sizeof(*communication));
}
