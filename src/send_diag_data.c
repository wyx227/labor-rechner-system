/* Interactive loopback PD switch demo for five BOOL8 signals. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "pd_communication.h"
#include "vos_utils.h"

#define LOOPBACK_IP             "127.0.0.1"
#define DOOR_OPEN_COMID         1100u
#define DOOR_CLOSED_COMID       1101u
#define BRAKING_COMID           1102u
#define RELEASE_BRAKING_COMID   1103u
#define EMERGENCY_BRAKE_COMID   1104u
#define SIGNAL_COUNT            5u

typedef struct
{
    BOOL8 door_open;
    BOOL8 door_closed;
    BOOL8 braking;
    BOOL8 release_braking;
    BOOL8 emergency_brake;
} SWITCH_STATE_T;

static volatile sig_atomic_t keep_running = 1;
static struct termios saved_terminal;
static int terminal_is_raw = 0;

static void restore_terminal (void)
{
    if (terminal_is_raw != 0)
    {
        (void) tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal);
        terminal_is_raw = 0;
    }
}

static void stop (int signal_number)
{
    (void) signal_number;
    keep_running = 0;
}

static int configure_terminal (void)
{
    struct termios raw_terminal;

    if (tcgetattr(STDIN_FILENO, &saved_terminal) != 0)
    {
        return 0;
    }
    raw_terminal = saved_terminal;
    raw_terminal.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw_terminal.c_cc[VMIN] = 0;
    raw_terminal.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_terminal) != 0)
    {
        return 0;
    }
    terminal_is_raw = 1;
    return 1;
}

static const char *bool_text (BOOL8 value)
{
    return value == TRUE ? "True" : "False";
}

static void draw_state (const SWITCH_STATE_T *state)
{
    printf("\033[2J\033[H");
    printf("PD loopback switch demo (127.0.0.1:17224)\n\n");
    printf("ComID 1100  isDoorOpen:       %s\n", bool_text(state->door_open));
    printf("ComID 1101  isDoorClosed:     %s\n", bool_text(state->door_closed));
    printf("ComID 1102  isBraking:        %s\n", bool_text(state->braking));
    printf("ComID 1103  isReleaseBraking: %s\n", bool_text(state->release_braking));
    printf("ComID 1104  isEmergencyBr:    %s\n\n", bool_text(state->emergency_brake));
    printf("F1: toggle door   F2: toggle brake   F3: toggle emergency brake   q: quit\n");
    fflush(stdout);
}

static TRDP_ERR_T create_publishers (PD_COMM_T *communication,
                                     TRDP_PUB_T publishers[SIGNAL_COUNT],
                                     const SWITCH_STATE_T *state)
{
    const UINT32 comids[SIGNAL_COUNT] = {
        DOOR_OPEN_COMID, DOOR_CLOSED_COMID, BRAKING_COMID,
        RELEASE_BRAKING_COMID, EMERGENCY_BRAKE_COMID
    };
    const BOOL8 values[SIGNAL_COUNT] = {
        state->door_open, state->door_closed, state->braking,
        state->release_braking, state->emergency_brake
    };
    TRDP_IP_ADDR_T destination = vos_dottedIP(LOOPBACK_IP);
    UINT32 index;
    TRDP_ERR_T error;

    for (index = 0u; index < SIGNAL_COUNT; ++index)
    {
        error = tlp_publish(communication->session, &publishers[index],
                            NULL, NULL, 0u, comids[index], 0u, 0u,
                            communication->config.source_ip, destination,
                            PD_COMM_DEFAULT_CYCLE_US, 0u, TRDP_FLAGS_NONE,
                            &values[index], sizeof(values[index]));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
    }
    return tlc_updateSession(communication->session);
}

static void destroy_publishers (PD_COMM_T *communication, TRDP_PUB_T publishers[SIGNAL_COUNT])
{
    UINT32 index;

    for (index = 0u; index < SIGNAL_COUNT; ++index)
    {
        if (publishers[index] != NULL)
        {
            (void) tlp_unpublish(communication->session, publishers[index]);
        }
    }
}

static TRDP_ERR_T publish_state (PD_COMM_T *communication,
                                 TRDP_PUB_T publishers[SIGNAL_COUNT],
                                 const SWITCH_STATE_T *state)
{
    const BOOL8 values[SIGNAL_COUNT] = {
        state->door_open, state->door_closed, state->braking,
        state->release_braking, state->emergency_brake
    };
    UINT32 index;
    TRDP_ERR_T error;

    for (index = 0u; index < SIGNAL_COUNT; ++index)
    {
        error = tlp_put(communication->session, publishers[index], &values[index], sizeof(values[index]));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
    }
    return TRDP_NO_ERR;
}

/* Recognise common xterm/VT and macOS terminal F1-F3 escape sequences. */
static int read_console_action (void)
{
    static char sequence[8];
    static size_t sequence_length = 0u;
    char input[32];
    ssize_t count;
    ssize_t index;

    count = read(STDIN_FILENO, input, sizeof(input));
    if (count <= 0)
    {
        return 0;
    }
    for (index = 0; index < count; ++index)
    {
        char character = input[index];

        if (sequence_length == 0u && character == 'q')
        {
            return 4;
        }
        if (sequence_length < sizeof(sequence) - 1u)
        {
            sequence[sequence_length++] = character;
            sequence[sequence_length] = '\0';
        }
        else
        {
            sequence_length = 0u;
        }
        if (strcmp(sequence, "\033OP") == 0 || strcmp(sequence, "\033[11~") == 0)
        {
            sequence_length = 0u;
            return 1;
        }
        if (strcmp(sequence, "\033OQ") == 0 || strcmp(sequence, "\033[12~") == 0)
        {
            sequence_length = 0u;
            return 2;
        }
        if (strcmp(sequence, "\033OR") == 0 || strcmp(sequence, "\033[13~") == 0)
        {
            sequence_length = 0u;
            return 3;
        }
        if (sequence[0] != '\033' || sequence_length >= 5u)
        {
            sequence_length = 0u;
        }
    }
    return 0;
}

int main (void)
{
    PD_COMM_CONFIG_T config;
    PD_COMM_T communication;
    TRDP_PUB_T publishers[SIGNAL_COUNT] = {NULL, NULL, NULL, NULL, NULL};
    SWITCH_STATE_T state = {FALSE, TRUE, FALSE, FALSE, FALSE};
    TRDP_ERR_T error;

    if (!configure_terminal())
    {
        fprintf(stderr, "This demo requires an interactive terminal: %s\n", strerror(errno));
        return 1;
    }
    atexit(restore_terminal);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    pd_comm_config_init(&config);
    error = pd_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = create_publishers(&communication, publishers, &state);
    }
    if (error != TRDP_NO_ERR)
    {
        restore_terminal();
        fprintf(stderr, "PD setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        destroy_publishers(&communication, publishers);
        pd_comm_close(&communication);
        return 1;
    }

    draw_state(&state);
    while (keep_running)
    {
        struct timeval timeout = {0, 100000};
        fd_set input_fds;
        int action;

        FD_ZERO(&input_fds);
        FD_SET(STDIN_FILENO, &input_fds);
        (void) select(STDIN_FILENO + 1, &input_fds, NULL, NULL, &timeout);
        action = FD_ISSET(STDIN_FILENO, &input_fds) ? read_console_action() : 0;
        if (action == 1)
        {
            state.door_open = state.door_open == TRUE ? FALSE : TRUE;
            state.door_closed = state.door_open == TRUE ? FALSE : TRUE;
            draw_state(&state);
        }
        else if (action == 2)
        {
            state.braking = state.braking == TRUE ? FALSE : TRUE;
            state.release_braking = state.braking == TRUE ? FALSE : TRUE;
            draw_state(&state);
        }
        else if (action == 3)
        {
            state.emergency_brake = state.emergency_brake == TRUE ? FALSE : TRUE;
            draw_state(&state);
        }
        else if (action == 4)
        {
            keep_running = 0;
        }

        error = publish_state(&communication, publishers, &state);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD update failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
        error = pd_comm_process(&communication);
        if (error != TRDP_NO_ERR)
        {
            fprintf(stderr, "PD process failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
            break;
        }
    }

    destroy_publishers(&communication, publishers);
    pd_comm_close(&communication);
    return error == TRDP_NO_ERR ? 0 : 1;
}
