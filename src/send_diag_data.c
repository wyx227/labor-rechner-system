/*
 * PD teaching simulator.
 *
 * Reads IPv4 destinations from /tmp/gruppe-a.txt and unicast-publishes:
 *   ComID 1000: actSpeed   (UINT8)
 *   ComID 1001: isDoorOpen (BOOL8, 0 or 1)
 *
 * A Unix datagram socket accepts EXCEPTION_1, EXCEPTION_2 and RESET.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

#include "pd_communication.h"
#include "vos_utils.h"

#define TARGET_LIST_PATH        "/tmp/gruppe-a.txt"
#define CONTROL_SOCKET_PATH     "/tmp/pd-teaching-simulator.sock"
#define MAX_TARGETS             128u
#define SPEED_COMID             1000u
#define DOOR_COMID              1001u
#define UPDATE_INTERVAL_US      1000000L

typedef enum
{
    MODE_NORMAL,
    MODE_EXCEPTION_1,
    MODE_EXCEPTION_2
} SIMULATION_MODE_T;

typedef struct
{
    TRDP_IP_ADDR_T address;
    TRDP_PUB_T speed_publisher;
    TRDP_PUB_T door_publisher;
} TARGET_T;

typedef struct
{
    UINT8 speed;
    BOOL8 door_open;
    UINT32 dwell_ticks;
    UINT32 cruise_ticks;
    BOOL8 braking;
    SIMULATION_MODE_T mode;
} TRAIN_STATE_T;

static volatile sig_atomic_t keep_running = 1;

static void stop (int signal_number)
{
    (void) signal_number;
    keep_running = 0;
}

static void usage (const char *program)
{
    fprintf(stderr, "Usage: %s [SOURCE_IP]\n", program);
}

static long elapsed_microseconds (const struct timeval *before, const struct timeval *after)
{
    return (after->tv_sec - before->tv_sec) * 1000000L + after->tv_usec - before->tv_usec;
}

static void trim_line (char *text)
{
    char *comment = strchr(text, '#');
    char *end;

    if (comment != NULL)
    {
        *comment = '\0';
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
        *--end = '\0';
    }
}

static int read_targets (TARGET_T *targets, UINT32 *target_count)
{
    FILE *file;
    char line[64];
    UINT32 count = 0u;

    file = fopen(TARGET_LIST_PATH, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Cannot open %s: %s\n", TARGET_LIST_PATH, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        TRDP_IP_ADDR_T address;

        trim_line(line);
        if (line[0] == '\0')
        {
            continue;
        }
        address = vos_dottedIP(line);
        if (address == 0u || count == MAX_TARGETS)
        {
            fprintf(stderr, "Ignoring invalid or excess destination: %s\n", line);
            continue;
        }
        targets[count].address = address;
        count++;
    }
    fclose(file);

    if (count == 0u)
    {
        fprintf(stderr, "%s contains no usable IPv4 destinations.\n", TARGET_LIST_PATH);
        return 0;
    }
    *target_count = count;
    return 1;
}

static TRDP_ERR_T create_publishers (PD_COMM_T *communication, TARGET_T *targets, UINT32 target_count)
{
    UINT32 index;
    UINT8 initial_speed = 0u;
    BOOL8 initial_door_open = TRUE;
    TRDP_ERR_T error;

    for (index = 0u; index < target_count; ++index)
    {
        error = tlp_publish(communication->session, &targets[index].speed_publisher,
                            NULL, NULL, 0u, SPEED_COMID, 0u, 0u,
                            communication->config.source_ip, targets[index].address,
                            PD_COMM_DEFAULT_CYCLE_US, 0u, TRDP_FLAGS_NONE,
                            &initial_speed, sizeof(initial_speed));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
        error = tlp_publish(communication->session, &targets[index].door_publisher,
                            NULL, NULL, 0u, DOOR_COMID, 0u, 0u,
                            communication->config.source_ip, targets[index].address,
                            PD_COMM_DEFAULT_CYCLE_US, 0u, TRDP_FLAGS_NONE,
                            &initial_door_open, sizeof(initial_door_open));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
    }
    return tlc_updateSession(communication->session);
}

static void destroy_publishers (PD_COMM_T *communication, TARGET_T *targets, UINT32 target_count)
{
    UINT32 index;

    for (index = 0u; index < target_count; ++index)
    {
        (void) tlp_unpublish(communication->session, targets[index].speed_publisher);
        (void) tlp_unpublish(communication->session, targets[index].door_publisher);
    }
}

static TRDP_ERR_T publish_state (PD_COMM_T *communication,
                                 const TARGET_T *targets,
                                 UINT32 target_count,
                                 const TRAIN_STATE_T *state)
{
    UINT32 index;
    UINT8 speed = state->speed;
    BOOL8 door_open = state->door_open == TRUE ? TRUE : FALSE;
    TRDP_ERR_T error;

    for (index = 0u; index < target_count; ++index)
    {
        error = tlp_put(communication->session, targets[index].speed_publisher, &speed, sizeof(speed));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
        error = tlp_put(communication->session, targets[index].door_publisher, &door_open, sizeof(door_open));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
    }
    return TRDP_NO_ERR;
}

static void reset_state (TRAIN_STATE_T *state, SIMULATION_MODE_T mode)
{
    state->speed = 0u;
    state->door_open = mode == MODE_EXCEPTION_2 ? FALSE : TRUE;
    state->dwell_ticks = 0u;
    state->cruise_ticks = 0u;
    state->braking = FALSE;
    state->mode = mode;
}

static void advance_state (TRAIN_STATE_T *state)
{
    /* Slow acceleration and braking create believable speed changes. */
    if (state->speed == 0u)
    {
        if (state->dwell_ticks < 8u)
        {
            state->dwell_ticks++;
            state->door_open = TRUE;
        }
        else
        {
            state->door_open = FALSE;
            state->speed = 3u;
            state->braking = FALSE;
        }
    }
    else if (state->braking == TRUE)
    {
        state->speed = state->speed > 3u ? state->speed - 3u : 0u;
        state->door_open = FALSE;
    }
    else if (state->speed < 80u)
    {
        state->speed = state->speed > 77u ? 80u : state->speed + 3u;
        state->door_open = FALSE;
    }
    else if (state->cruise_ticks < 20u)
    {
        state->cruise_ticks++;
        state->door_open = FALSE;
    }
    else
    {
        state->braking = TRUE;
        state->speed -= 3u;
        state->door_open = FALSE;
    }

    if (state->mode == MODE_EXCEPTION_1 && state->speed != 0u)
    {
        state->door_open = TRUE;
    }
}

static int create_control_socket (void)
{
    int socket_fd;
    struct sockaddr_un address;
    struct stat path_info;

    socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socket_fd < 0)
    {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    (void) snprintf(address.sun_path, sizeof(address.sun_path), "%s", CONTROL_SOCKET_PATH);
    if (lstat(CONTROL_SOCKET_PATH, &path_info) == 0)
    {
        if (!S_ISSOCK(path_info.st_mode))
        {
            errno = EEXIST;
            (void) close(socket_fd);
            return -1;
        }
        if (unlink(CONTROL_SOCKET_PATH) < 0)
        {
            (void) close(socket_fd);
            return -1;
        }
    }
    else if (errno != ENOENT)
    {
        (void) close(socket_fd);
        return -1;
    }
    if (bind(socket_fd, (const struct sockaddr *) &address, sizeof(address)) < 0)
    {
        (void) close(socket_fd);
        return -1;
    }
    return socket_fd;
}

static int handle_control_messages (int socket_fd, TRAIN_STATE_T *state)
{
    char command[64];
    ssize_t received;
    int changed = 0;

    for (;;)
    {
        received = recv(socket_fd, command, sizeof(command) - 1u, MSG_DONTWAIT);
        if (received < 0)
        {
            return changed;
        }
        command[received] = '\0';
        command[strcspn(command, "\r\n")] = '\0';
        if (strcmp(command, "EXCEPTION_1") == 0)
        {
            state->mode = MODE_EXCEPTION_1;
            if (state->speed != 0u)
            {
                state->door_open = TRUE;
            }
            printf("Enabled EXCEPTION_1: doors can be open while moving.\n");
            changed = 1;
        }
        else if (strcmp(command, "EXCEPTION_2") == 0)
        {
            reset_state(state, MODE_EXCEPTION_2);
            printf("Enabled EXCEPTION_2: reset with doors closed.\n");
            changed = 1;
        }
        else if (strcmp(command, "RESET") == 0)
        {
            reset_state(state, MODE_NORMAL);
            printf("Reset normal train state.\n");
            changed = 1;
        }
        else
        {
            fprintf(stderr, "Ignoring unknown control command: %s\n", command);
        }
    }
}

int main (int argc, char *argv[])
{
    TARGET_T targets[MAX_TARGETS];
    UINT32 target_count;
    PD_COMM_CONFIG_T config;
    PD_COMM_T communication;
    TRAIN_STATE_T state;
    struct timeval last_update;
    int control_socket;
    TRDP_ERR_T error;

    if (argc > 2)
    {
        usage(argv[0]);
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0)
    {
        usage(argv[0]);
        return 0;
    }
    if (!read_targets(targets, &target_count))
    {
        return 1;
    }

    pd_comm_config_init(&config);
    if (argc == 2 && pd_comm_config_set_source_ip(&config, argv[1]) != TRDP_NO_ERR)
    {
        fprintf(stderr, "Invalid source IPv4 address.\n");
        return 1;
    }
    error = pd_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = create_publishers(&communication, targets, target_count);
    }
    if (error != TRDP_NO_ERR)
    {
        fprintf(stderr, "PD setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        pd_comm_close(&communication);
        return 1;
    }

    control_socket = create_control_socket();
    if (control_socket < 0)
    {
        fprintf(stderr, "Cannot bind %s: %s\n", CONTROL_SOCKET_PATH, strerror(errno));
        destroy_publishers(&communication, targets, target_count);
        pd_comm_close(&communication);
        return 1;
    }

    reset_state(&state, MODE_NORMAL);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    (void) gettimeofday(&last_update, NULL);
    printf("Publishing ComIDs 1000/1001 to %u unicast targets from %s.\n",
           target_count, TARGET_LIST_PATH);
    printf("Control socket: %s (EXCEPTION_1, EXCEPTION_2, RESET).\n", CONTROL_SOCKET_PATH);

    while (keep_running)
    {
        struct timeval timeout = {0, 100000};
        struct timeval now;
        fd_set read_fds;

        FD_ZERO(&read_fds);
        FD_SET(control_socket, &read_fds);
        (void) select(control_socket + 1, &read_fds, NULL, NULL, &timeout);
        if (FD_ISSET(control_socket, &read_fds))
        {
            if (handle_control_messages(control_socket, &state) != 0)
            {
                error = publish_state(&communication, targets, target_count, &state);
                if (error != TRDP_NO_ERR)
                {
                    fprintf(stderr, "PD control update failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
                    break;
                }
            }
        }

        error = pd_comm_process(&communication);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
        (void) gettimeofday(&now, NULL);
        if (elapsed_microseconds(&last_update, &now) >= UPDATE_INTERVAL_US)
        {
            advance_state(&state);
            error = publish_state(&communication, targets, target_count, &state);
            if (error != TRDP_NO_ERR)
            {
                fprintf(stderr, "PD update failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
                break;
            }
            last_update = now;
        }
    }

    (void) close(control_socket);
    (void) unlink(CONTROL_SOCKET_PATH);
    destroy_publishers(&communication, targets, target_count);
    pd_comm_close(&communication);
    return error == TRDP_NO_ERR ? 0 : 1;
}
