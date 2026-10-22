#ifndef CONNECTION_H
#define CONNECTION_H

/* Serve one client on an already connected socket until the client
 * closes it or an error occurs. Runs inside the child process.
 * Returns 0 for a normal end, -1 if the connection was dropped. */
int serve_client(int fd);

#endif
