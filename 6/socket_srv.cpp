//***************************************************************************
//
// Program example for labs in subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2017
//
// Example of socket server.
//
// This program is example of socket server and it allows to connect and serve
// the only one client.
// The mandatory argument of program is port number for listening.
//
//***************************************************************************

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <semaphore.h>

#define STR_CLOSE "close"
#define STR_QUIT "quit"

//***************************************************************************
// log messages

#define LOG_ERROR 0 // errors
#define LOG_INFO 1  // information and notifications
#define LOG_DEBUG 2 // debug messages

// debug flag
int g_debug = LOG_INFO;

void log_msg(int t_log_level, const char *t_form, ...)
{
    const char *out_fmt[] = {
        "ERR: (%d-%s) %s\n",
        "INF: %s\n",
        "DEB: %s\n"};

    if (t_log_level && t_log_level > g_debug)
        return;

    char l_buf[1024];
    va_list l_arg;
    va_start(l_arg, t_form);
    vsprintf(l_buf, t_form, l_arg);
    va_end(l_arg);

    switch (t_log_level)
    {
    case LOG_INFO:
    case LOG_DEBUG:
        fprintf(stdout, out_fmt[t_log_level], l_buf);
        break;

    case LOG_ERROR:
        fprintf(stderr, out_fmt[t_log_level], errno, strerror(errno), l_buf);
        break;
    }
}

//***************************************************************************
// help

void help(int t_narg, char **t_args)
{
    if (t_narg <= 1 || !strcmp(t_args[1], "-h"))
    {
        printf(
            "\n"
            "  Socket server example.\n"
            "\n"
            "  Use: %s [-h -d] port_number\n"
            "\n"
            "    -d  debug mode \n"
            "    -h  this help\n"
            "\n",
            t_args[0]);

        exit(0);
    }

    if (!strcmp(t_args[1], "-d"))
        g_debug = LOG_DEBUG;
}

//***************************************************************************

#define MAX_CLIENT 2
#define SHM_NAME "piskvorky"

int numberOfClient = 0;

struct shm_data
{
    char gameBoard[3][4];
};

// pointer to shared memory
shm_data *g_glb_data = nullptr;

sem_t *semPlayerOne = nullptr;
sem_t *semPlayerTwo = nullptr;

void restart()
{
for (int i = 0; i < 3; i++)
        {
            
            strcpy(g_glb_data->gameBoard[i], "---");
            
        }
}

void acceptClient(int l_sock_client, int l_sock_listen, int playerID)
{
    printf("New player with ID: %d\n", playerID);
    pollfd l_read_poll[2];

    l_read_poll[0].fd = STDIN_FILENO;
    l_read_poll[0].events = POLLIN;
    l_read_poll[1].fd = l_sock_client;
    l_read_poll[1].events = POLLIN;

    int pid = fork();

    bool played = false;

    if (pid == 0)
    {
        while (1)
        { // communication
            switch (playerID)
            {
            case 0:
                log_msg(LOG_INFO, "Player one waiting");
                sem_wait(semPlayerOne);
                log_msg(LOG_INFO, "Player one finished waiting");
                break;

            case 1:
                log_msg(LOG_INFO, "Player two waiting");
                sem_wait(semPlayerTwo);
                log_msg(LOG_INFO, "Player two finished waiting");
                break;
            }

            for (int i = 0; i < 3; i++)
            {
                write(l_sock_client, g_glb_data->gameBoard[i], strlen(g_glb_data->gameBoard[i]));
                write(l_sock_client, "\n", strlen("\n"));
            }

            // select from fds
            int l_poll = poll(l_read_poll, 2, -1);

            if (l_poll < 0)
            {
                log_msg(LOG_ERROR, "Function poll failed!");
                exit(1);
            }

            char l_buf[256];

            // data from client?
            if (l_read_poll[1].revents & POLLIN)
            {
                // read data from socket
                int l_len = read(l_sock_client, l_buf, sizeof(l_buf));
                if (!l_len)
                {
                    log_msg(LOG_DEBUG, "Client closed socket!");
                    close(l_sock_client);
                    break;
                }
                else if (l_len < 0)
                {
                    log_msg(LOG_ERROR, "Unable to read data from client.");
                    close(l_sock_client);
                    break;
                }
                else
                    log_msg(LOG_DEBUG, "Read %d bytes from client.", l_len);

                char *znak = strtok(l_buf, "-");
                char *xAxis = strtok(NULL, "-");
                char *yAxis = strtok(NULL, "-");

                if (znak != NULL)
                {
                    int x = atoi(xAxis);
                    int y = atoi(yAxis);

                    g_glb_data->gameBoard[x][y] = *znak;

                    played = true;
                }

                switch (playerID)
                {
                case 0:
                    sem_post(semPlayerTwo);
                    break;

                case 1:
                    sem_post(semPlayerOne);
                    break;
                }

                // close request?
                if (!strncasecmp(l_buf, "close", strlen(STR_CLOSE)))
                {
                    log_msg(LOG_INFO, "Client sent 'close' request to close connection.");
                    close(l_sock_client);
                    log_msg(LOG_INFO, "Connection closed. Waiting for new client.");
                    numberOfClient--;
                    break;
                }
            }
            // request for quit
            if (!strncasecmp(l_buf, "quit", strlen(STR_QUIT)))
            {
                close(l_sock_listen);
                close(l_sock_client);
                log_msg(LOG_INFO, "Request to 'quit' entered");
                exit(0);
            }
        } // while communication
    }
}

int main(int t_narg, char **t_args)
{
    if (t_narg <= 1)
        help(t_narg, t_args);

    int l_port = 0;

    // parsing arguments
    for (int i = 1; i < t_narg; i++)
    {
        if (!strcmp(t_args[i], "-d"))
            g_debug = LOG_DEBUG;

        if (!strcmp(t_args[i], "-h"))
            help(t_narg, t_args);

        if (*t_args[i] != '-' && !l_port)
        {
            l_port = atoi(t_args[i]);
            break;
        }
    }

    if (l_port <= 0)
    {
        log_msg(LOG_INFO, "Bad or missing port number %d!", l_port);
        help(t_narg, t_args);
    }

    log_msg(LOG_INFO, "Server will listen on port: %d.", l_port);

    // socket creation
    int l_sock_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (l_sock_listen == -1)
    {
        log_msg(LOG_ERROR, "Unable to create socket.");
        exit(1);
    }

    in_addr l_addr_any = {INADDR_ANY};
    sockaddr_in l_srv_addr;
    l_srv_addr.sin_family = AF_INET;
    l_srv_addr.sin_port = htons(l_port);
    l_srv_addr.sin_addr = l_addr_any;

    // Enable the port number reusing
    int l_opt = 1;
    if (setsockopt(l_sock_listen, SOL_SOCKET, SO_REUSEADDR, &l_opt, sizeof(l_opt)) < 0)
        log_msg(LOG_ERROR, "Unable to set socket option!");

    // assign port number to socket
    if (bind(l_sock_listen, (const sockaddr *)&l_srv_addr, sizeof(l_srv_addr)) < 0)
    {
        log_msg(LOG_ERROR, "Bind failed!");
        close(l_sock_listen);
        exit(1);
    }

    // listenig on set port
    if (listen(l_sock_listen, 1) < 0)
    {
        log_msg(LOG_ERROR, "Unable to listen on given port!");
        close(l_sock_listen);
        exit(1);
    }

    log_msg(LOG_INFO, "Enter 'quit' to quit server.");

    int l_first = 0;

    int l_fd = shm_open(SHM_NAME, O_RDWR, 0660);
    if (l_fd < 0)
    {
        log_msg(LOG_ERROR, "Unable to open file for shared memory.");
        l_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0660);
        if (l_fd < 0)
        {
            log_msg(LOG_ERROR, "Unable to create file for shared memory.");
            exit(1);
        }
        ftruncate(l_fd, sizeof(shm_data));
        log_msg(LOG_INFO, "File created, this process is first");
        l_first = 1;
    }

    g_glb_data = (shm_data *)mmap(nullptr, sizeof(shm_data), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, l_fd, 0);

    if (!g_glb_data)
    {
        log_msg(LOG_ERROR, "Unable to attach shared memory!");
        exit(1);
    }
    else
        log_msg(LOG_INFO, "Shared memory attached.");

    if (l_first)
    {
        for (int i = 0; i < 3; i++)
        {
            
            strcpy(g_glb_data->gameBoard[i], "---");
            
        }
    }
    sem_unlink("playerOne");
    sem_unlink("playerTwo");
    semPlayerOne = sem_open("playerOne", O_RDWR | O_CREAT, 0660, 1);
    if (!semPlayerOne)
    {
        log_msg(LOG_ERROR, "Unable to create player one semaphore");
        return 1;
    }

    log_msg(LOG_INFO, "Player one semaphore created");

    semPlayerTwo = sem_open("playerTwo", O_RDWR | O_CREAT, 0660, 0);
    if (!semPlayerTwo)
    {
        log_msg(LOG_ERROR, "Unable to create player two semaphore");
        return 1;
    }

    log_msg(LOG_INFO, "Player two semaphore created");
    // go!
    while (1)
    {
        int l_sock_client = -1;

        // list of fd sources
        pollfd l_read_poll[2];

        l_read_poll[0].fd = STDIN_FILENO;
        l_read_poll[0].events = POLLIN;
        l_read_poll[1].fd = l_sock_listen;
        l_read_poll[1].events = POLLIN;

        while (1) // wait for new client
        {
            // select from fds
            int l_poll = poll(l_read_poll, 2, -1);

            if (l_poll < 0)
            {
                log_msg(LOG_ERROR, "Function poll failed!");
                exit(1);
            }

            if (l_read_poll[0].revents & POLLIN)
            { // data on stdin
                char buf[128];
                int len = read(STDIN_FILENO, buf, sizeof(buf));
                if (len < 0)
                {
                    log_msg(LOG_DEBUG, "Unable to read from stdin!");
                    exit(1);
                }

                log_msg(LOG_DEBUG, "Read %d bytes from stdin");
                // request to quit?
                if (!strncmp(buf, STR_QUIT, strlen(STR_QUIT)))
                {
                    log_msg(LOG_INFO, "Request to 'quit' entered.");
                    close(l_sock_listen);
                    sem_close(semPlayerOne);
                    sem_close(semPlayerTwo);
                    sem_unlink("playerOne");
                    sem_unlink("playerTwo");
                    exit(0);
                }
                // request to quit?
                if (!strncmp(buf, "restart", strlen("restart")))
                {
                    restart();
                    log_msg(LOG_INFO, "Restarting gameboard");
                }
            }

            if (l_read_poll[1].revents & POLLIN)
            { // new client?
                sockaddr_in l_rsa;
                int l_rsa_size = sizeof(l_rsa);
                // new connection
                l_sock_client = accept(l_sock_listen, (sockaddr *)&l_rsa, (socklen_t *)&l_rsa_size);
                if (l_sock_client == -1)
                {
                    log_msg(LOG_ERROR, "Unable to accept new client.");
                    close(l_sock_listen);
                    exit(1);
                }
                uint l_lsa = sizeof(l_srv_addr);
                // my IP
                getsockname(l_sock_client, (sockaddr *)&l_srv_addr, &l_lsa);
                log_msg(LOG_INFO, "My IP: '%s'  port: %d",
                        inet_ntoa(l_srv_addr.sin_addr), ntohs(l_srv_addr.sin_port));
                // client IP
                getpeername(l_sock_client, (sockaddr *)&l_srv_addr, &l_lsa);
                log_msg(LOG_INFO, "Client IP: '%s'  port: %d",
                        inet_ntoa(l_srv_addr.sin_addr), ntohs(l_srv_addr.sin_port));

                if (numberOfClient == 2)
                {
                    write(l_sock_client, "SERVER FULL\n", strlen("SERVER FULL\n"));
                    close(l_sock_client);
                }

                break;
            }

        } // while wait for client

        acceptClient(l_sock_client, l_sock_listen, numberOfClient++);

    } // while ( 1 )

    return 0;
}
