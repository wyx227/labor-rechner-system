/* Interactive loopback PD temperature exploration demo. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "pd_communication.h"
#include "vos_utils.h"

#define LOOPBACK_IP        "127.0.0.1"
#define FIRST_COMID        1100u
#define BOGIE_COUNT        4u
#define UPDATE_INTERVAL_MS 500u

typedef struct
{
    TRDP_PUB_T publisher;
    UINT8 temperature;
    BOOL8 overheat;
} BOGIE_T;

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

static int clamp (int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static void advance_temperature (BOGIE_T *bogie)
{
    int temperature = bogie->temperature;
    int change;

    if (bogie->overheat == TRUE)
    {
        /* The event becomes visible through a gradual, not instantaneous, rise. */
        if (temperature < 125)
        {
            temperature += 2 + rand() % 4;
        }
        else
        {
            change = (rand() % 5) - 2;
            temperature = clamp(temperature + change, 121, 180);
        }
    }
    else if (temperature > 90)
    {
        temperature -= 1 + rand() % 3;
    }
    else
    {
        change = (rand() % 5) - 2;
        temperature = clamp(temperature + change, 35, 95);
    }
    bogie->temperature = (UINT8) clamp(temperature, 0, 255);
}

static void draw_state (const BOGIE_T bogies[BOGIE_COUNT])
{
    UINT32 index;

    printf("\033[2J\033[H");
    printf("PD temperature exploration demo (127.0.0.1:17224)\n\n");
    printf("ComID   Drehgestell   actTemp   overheat\n");
    for (index = 0u; index < BOGIE_COUNT; ++index)
    {
        printf("%5u   %11u   %3u C    %s\n", FIRST_COMID + index, index + 1u,
               bogies[index].temperature, bogies[index].overheat == TRUE ? "ON" : "off");
    }
    printf("\nF1-F4: toggle overheat for Drehgestell 1-4    q: quit\n");
    fflush(stdout);
}

static TRDP_ERR_T create_publishers (PD_COMM_T *communication, BOGIE_T bogies[BOGIE_COUNT])
{
    UINT32 index;
    TRDP_IP_ADDR_T destination = vos_dottedIP(LOOPBACK_IP);
    TRDP_ERR_T error;

    for (index = 0u; index < BOGIE_COUNT; ++index)
    {
        error = tlp_publish(communication->session, &bogies[index].publisher,
                            NULL, NULL, 0u, FIRST_COMID + index, 0u, 0u,
                            communication->config.source_ip, destination,
                            PD_COMM_DEFAULT_CYCLE_US, 0u, TRDP_FLAGS_NONE,
                            &bogies[index].temperature, sizeof(bogies[index].temperature));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
    }
    return tlc_updateSession(communication->session);
}

static void destroy_publishers (PD_COMM_T *communication, const BOGIE_T bogies[BOGIE_COUNT])
{
    UINT32 index;

    for (index = 0u; index < BOGIE_COUNT; ++index)
    {
        (void) tlp_unpublish(communication->session, bogies[index].publisher);
    }
}

static TRDP_ERR_T publish_temperatures (PD_COMM_T *communication, const BOGIE_T bogies[BOGIE_COUNT])
{
    UINT32 index;
    TRDP_ERR_T error;

    for (index = 0u; index < BOGIE_COUNT; ++index)
    {
        error = tlp_put(communication->session, bogies[index].publisher,
                        &bogies[index].temperature, sizeof(bogies[index].temperature));
        if (error != TRDP_NO_ERR)
        {
            return error;
        }
    }
    return TRDP_NO_ERR;
}

/* Recognise common xterm/VT and macOS terminal F1-F4 escape sequences. */
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
            return 5;
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
        if (strcmp(sequence, "\033OS") == 0 || strcmp(sequence, "\033[14~") == 0)
        {
            sequence_length = 0u;
            return 4;
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
    BOGIE_T bogies[BOGIE_COUNT] = {
        {NULL, 68u, FALSE}, {NULL, 71u, FALSE}, {NULL, 66u, FALSE}, {NULL, 73u, FALSE}
    };
    PD_COMM_CONFIG_T config;
    PD_COMM_T communication;
    TRDP_ERR_T error;

    if (!configure_terminal())
    {
        fprintf(stderr, "This demo requires an interactive terminal: %s\n", strerror(errno));
        return 1;
    }
    atexit(restore_terminal);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    srand((unsigned int) (time(NULL) ^ (time_t) getpid()));

    pd_comm_config_init(&config);
    error = pd_comm_open(&communication, &config);
    if (error == TRDP_NO_ERR)
    {
        error = create_publishers(&communication, bogies);
    }
    if (error != TRDP_NO_ERR)
    {
        restore_terminal();
        fprintf(stderr, "PD setup failed: %s\n", vos_getErrorString((VOS_ERR_T) error));
        pd_comm_close(&communication);
        return 1;
    }

    draw_state(bogies);
    while (keep_running)
    {
        struct timeval timeout = {0, UPDATE_INTERVAL_MS * 1000u};
        fd_set input_fds;
        int action;
        UINT32 index;

        FD_ZERO(&input_fds);
        FD_SET(STDIN_FILENO, &input_fds);
        (void) select(STDIN_FILENO + 1, &input_fds, NULL, NULL, &timeout);
        action = FD_ISSET(STDIN_FILENO, &input_fds) ? read_console_action() : 0;
        if (action >= 1 && action <= 4)
        {
            index = (UINT32) (action - 1);
            bogies[index].overheat = bogies[index].overheat == TRUE ? FALSE : TRUE;
        }
        else if (action == 5)
        {
            keep_running = 0;
        }

        for (index = 0u; index < BOGIE_COUNT; ++index)
        {
            advance_temperature(&bogies[index]);
        }
        draw_state(bogies);
        error = publish_temperatures(&communication, bogies);
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

    destroy_publishers(&communication, bogies);
    pd_comm_close(&communication);
    return error == TRDP_NO_ERR ? 0 : 1;
}
