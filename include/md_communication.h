#ifndef MD_COMMUNICATION_H
#define MD_COMMUNICATION_H

/* Lightweight wrapper around TRDP Message Data (MD) communication.
 *
 * MN (notification) and MR (request/reply) deliberately use separate API
 * entry points. Callbacks run synchronously inside md_comm_process(); do not
 * retain pMsg or pData after the callback returns.
 */

#if !defined(WIN32) && !defined(WIN64)
#include <sys/select.h>
#include <netinet/in.h>
#endif

#include "trdp_if_light.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MD_COMM_DEFAULT_REPLY_TIMEOUT_US 5000000u
#define MD_COMM_DEFAULT_RETRIES          2u

typedef struct
{
    UINT32          comid;
    BOOL8           use_udp;
    TRDP_IP_ADDR_T  destination_ip;
    TRDP_IP_ADDR_T  source_ip;
    UINT32          reply_timeout_us;
    UINT8           retries;
} MD_COMM_CONFIG_T;

typedef struct MD_COMM MD_COMM_T;

/* Called for an incoming MN notification or MR request respectively. */
typedef void (*MD_COMM_MN_CALLBACK_T)(
    void *user_ref, MD_COMM_T *communication, const TRDP_MD_INFO_T *message,
    UINT8 *data, UINT32 size);
typedef void (*MD_COMM_MR_CALLBACK_T)(
    void *user_ref, MD_COMM_T *communication, const TRDP_MD_INFO_T *message,
    UINT8 *data, UINT32 size);

struct MD_COMM
{
    TRDP_APP_SESSION_T      session;
    TRDP_LIS_T              listener;
    MD_COMM_CONFIG_T        config;
    MD_COMM_MN_CALLBACK_T   mn_callback;
    MD_COMM_MR_CALLBACK_T   mr_callback;
    void                    *listener_user_ref;
    BOOL8                   initialized;
    BOOL8                   listener_open;
};

void       md_comm_config_init (MD_COMM_CONFIG_T *config);
void       md_comm_config_set_comid (MD_COMM_CONFIG_T *config, UINT32 comid);
void       md_comm_config_set_udp (MD_COMM_CONFIG_T *config, BOOL8 use_udp);
TRDP_ERR_T md_comm_config_set_destination_ip (MD_COMM_CONFIG_T *config, const CHAR8 *address);
TRDP_ERR_T md_comm_config_set_source_ip (MD_COMM_CONFIG_T *config, const CHAR8 *address);

TRDP_ERR_T md_comm_open (MD_COMM_T *communication, const MD_COMM_CONFIG_T *config);
void       md_comm_close (MD_COMM_T *communication);

/* MN: notification with no application reply. callback may be NULL. */
TRDP_ERR_T md_comm_send_mn (MD_COMM_T *communication,
                            const UINT8 *data,
                            UINT32 size,
                            TRDP_MD_CALLBACK_T callback,
                            void *user_ref);
TRDP_ERR_T md_comm_send_mn_with_comid (MD_COMM_T *communication,
                                       UINT32 comid,
                                       const UINT8 *data,
                                       UINT32 size,
                                       TRDP_MD_CALLBACK_T callback,
                                       void *user_ref);

/* MR: request that expects replies. callback receives MP replies and request
 * timeout/error notifications. session_id may be retained for aborting. */
TRDP_ERR_T md_comm_send_mr (MD_COMM_T *communication,
                            const UINT8 *data,
                            UINT32 size,
                            UINT32 expected_replies,
                            TRDP_MD_CALLBACK_T callback,
                            void *user_ref,
                            TRDP_UUID_T *session_id);
TRDP_ERR_T md_comm_abort_mr (MD_COMM_T *communication, const TRDP_UUID_T *session_id);

/* Register one listener and independently dispatch incoming MN and MR data.
 * Either callback may be NULL. An MR callback can reply using md_comm_reply(). */
TRDP_ERR_T md_comm_listen (MD_COMM_T *communication,
                           MD_COMM_MN_CALLBACK_T mn_callback,
                           MD_COMM_MR_CALLBACK_T mr_callback,
                           void *user_ref);
TRDP_ERR_T md_comm_reply (MD_COMM_T *communication,
                          const TRDP_MD_INFO_T *request,
                          UINT32 user_status,
                          const UINT8 *data,
                          UINT32 size);

/* Wait for one MD interval and dispatch received traffic and callbacks. */
TRDP_ERR_T md_comm_process (MD_COMM_T *communication);

#ifdef __cplusplus
}
#endif

#endif /* MD_COMMUNICATION_H */
