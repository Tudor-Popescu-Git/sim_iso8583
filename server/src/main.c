#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "connection.h"

#define LISTEN_PORT 8583
#define BACKLOG     16

/* ------------------------------------------------------------------ */
/* TODO 1: install_sigchld_handler                                    */
/*                                                                    */
/* When a child process ends, it stays in the process table as a      */
/* "zombie" until the parent collects its exit status with waitpid(). */
/* Without this, a long-running server collects zombies.              */
/*                                                                    */
/*  - Write a handler: static void on_sigchld(int sig)                */
/*      { (void)sig; while (waitpid(-1, NULL, WNOHANG) > 0) {} }      */
/*    The loop is needed because several children may end while one   */
/*    signal is pending (signals are not queued).                     */
/*  - Install it with sigaction(), not signal():                      */
/*      sa.sa_handler = on_sigchld; sigemptyset(&sa.sa_mask);         */
/*      sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;                      */
/*  - Also ignore SIGPIPE:  signal(SIGPIPE, SIG_IGN);                 */
/*    (see the note in framing.c, write_all)                          */
/*  - Save and restore errno inside the handler if it calls anything  */
/*    that can change it. waitpid can.                                */
/* ------------------------------------------------------------------ */
static void install_sigchld_handler(void)
{
}

/* ------------------------------------------------------------------ */
/* TODO 2: create_listener                                            */
/*                                                                    */
/* Return a listening socket on the given port, or -1 on error.       */
/*  1. fd = socket(AF_INET, SOCK_STREAM, 0)                           */
/*  2. setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one)     */
/*     Without it, restarting the server fails with "Address already  */
/*     in use" for about a minute (old connections in TIME_WAIT).     */
/*  3. struct sockaddr_in addr; memset to 0;                          */
/*     addr.sin_family = AF_INET;                                     */
/*     addr.sin_addr.s_addr = htonl(INADDR_ANY);                      */
/*     addr.sin_port = htons(port);          <- byte order matters    */
/*  4. bind(fd, (struct sockaddr *)&addr, sizeof addr)                */
/*  5. listen(fd, BACKLOG)                                            */
/*  On any failure: perror("...") with the name of the call, close    */
/*  fd if it is open, return -1.                                      */
/* ------------------------------------------------------------------ */
static int create_listener(unsigned short port)
{
    (void)port;
    return -1;
}

int main(int argc, char **argv)
{
    unsigned short port = LISTEN_PORT;
    int lfd;

    if (argc > 1)
        port = (unsigned short)atoi(argv[1]);

    install_sigchld_handler();

    lfd = create_listener(port);
    if (lfd < 0) {
        fprintf(stderr, "sim_iso8583d: create_listener() is not implemented yet (TODO 2)\n");
        return 1;
    }
    fprintf(stderr, "sim_iso8583d: listening on port %u\n", (unsigned)port);

    for (;;) {
        /* TODO 3: accept loop.                                       */
        /*  a. cfd = accept(lfd, NULL, NULL)                          */
        /*     If it returns -1 and errno == EINTR (a signal arrived, */
        /*     for example SIGCHLD), just continue the loop.          */
        /*     Other errors: perror("accept"), continue.              */
        /*  b. pid = fork()                                           */
        /*     pid < 0  : perror("fork"); close(cfd); continue.       */
        /*     pid == 0 : CHILD.  close(lfd);  serve_client(cfd);     */
        /*                close(cfd);  _exit(0);                      */
        /*                Use _exit, not exit, so the child does not  */
        /*                flush buffers it shares with the parent.    */
        /*     pid > 0  : PARENT. close(cfd);  continue.              */
        /*  Both processes hold copies of both descriptors after      */
        /*  fork(). Each one must close the descriptor it does not    */
        /*  use, or the connection never ends and descriptors leak.   */
        /*                                                            */
        /* Remove the next line when TODO 3 is written.               */
        (void)serve_client;
        break;
    }

    close(lfd);
    return 0;
}
