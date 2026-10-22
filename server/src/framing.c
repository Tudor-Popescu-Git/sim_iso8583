#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "framing.h"
#include "sim_iso8583.h"      /* ISO_MAX_MSG */

/* ------------------------------------------------------------------ */
/* TODO: read_exact                                                   */
/*                                                                    */
/* TCP is a byte stream. One read() can return fewer bytes than you   */
/* asked for, even if the sender wrote them in one write().           */
/* So loop until n bytes have arrived:                                */
/*   - r = read(fd, p + done, n - done)                               */
/*   - r > 0   : done += r                                            */
/*   - r == 0  : peer closed. If done == 0 return 0, else return -1.  */
/*   - r < 0   : if errno == EINTR, try again; otherwise return -1.   */
/* Return 1 when done == n.                                           */
/* ------------------------------------------------------------------ */
int read_exact(int fd, void *buf, size_t n)
{
    (void)fd;
    (void)buf;
    (void)n;
    return -1;
}

/* ------------------------------------------------------------------ */
/* TODO: write_all                                                    */
/*                                                                    */
/* Same idea for writing: write() may accept fewer bytes than asked.  */
/* Loop until all n bytes are written. Retry on EINTR. Return -1 on   */
/* any other error. Return 0 when done.                               */
/* Note: writing to a socket the peer already closed raises SIGPIPE,  */
/* which ends the process. Either ignore SIGPIPE in main.c            */
/* (signal(SIGPIPE, SIG_IGN)) or use send() with MSG_NOSIGNAL.        */
/* ------------------------------------------------------------------ */
int write_all(int fd, const void *buf, size_t n)
{
    (void)fd;
    (void)buf;
    (void)n;
    return -1;
}

/* ------------------------------------------------------------------ */
/* TODO: read_frame                                                   */
/*                                                                    */
/*  1. unsigned char hdr[2]; r = read_exact(fd, hdr, 2)               */
/*       r == 0 -> return FRAME_CLOSED   (client finished normally)   */
/*       r <  0 -> return FRAME_ERR                                   */
/*  2. Decode the length. The wire format is big-endian:              */
/*       first byte = high 8 bits, second byte = low 8 bits.          */
/*     LEGACY (Day 1, deliberate):                                    */
/*       n = *(unsigned short *)hdr;     <- correct only on a         */
/*                                          big-endian CPU            */
/*     PORTABLE (Day 3):                                              */
/*       n = ((size_t)hdr[0] << 8) | hdr[1];                          */
/*     Run tools/throwaway_client.py after the legacy version on x86  */
/*     and look at what happens. That is the point of the exercise.   */
/*  3. If n == 0 or n > ISO_MAX_MSG or n > cap -> return FRAME_ERR.   */
/*     Do this BEFORE reading the body. A client must not be able to  */
/*     make the server wait for 60000 bytes.                          */
/*  4. r = read_exact(fd, buf, n). r != 1 -> return FRAME_ERR.        */
/*     (r == 0 here still means a truncated frame: header arrived,    */
/*     body did not. read_exact returns 0 only when zero bytes were   */
/*     read, so treat it as FRAME_ERR in this step.)                  */
/*  5. *len = n; return FRAME_OK.                                     */
/* ------------------------------------------------------------------ */
int read_frame(int fd, unsigned char *buf, size_t cap, size_t *len)
{
    (void)fd;
    (void)buf;
    (void)cap;
    (void)len;
    return FRAME_ERR;
}

/* ------------------------------------------------------------------ */
/* TODO: write_frame                                                  */
/*                                                                    */
/*  1. If len == 0 or len > ISO_MAX_MSG return -1.                    */
/*  2. Build the header: hdr[0] = (len >> 8) & 0xFF; hdr[1] = len & 0xFF */
/*  3. write_all(fd, hdr, 2), then write_all(fd, msg, len).           */
/*     Optional improvement: copy header and message into one buffer  */
/*     and call write_all once, so they leave in one TCP segment.     */
/* ------------------------------------------------------------------ */
int write_frame(int fd, const unsigned char *msg, size_t len)
{
    (void)fd;
    (void)msg;
    (void)len;
    return -1;
}

/* Implemented: hex dump to stderr, for example  rx (47 bytes): 30 32 30 30 ... */
void hex_dump(const char *label, const unsigned char *buf, size_t len)
{
    size_t i;
    fprintf(stderr, "%s (%lu bytes):", label, (unsigned long)len);
    for (i = 0; i < len; i++)
        fprintf(stderr, " %02X", buf[i]);
    fputc('\n', stderr);
}
