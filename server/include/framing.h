#ifndef FRAMING_H
#define FRAMING_H

#include <stddef.h>

/* Result of read_frame(). */
enum {
    FRAME_OK     =  0,   /* one complete frame is in the buffer                 */
    FRAME_CLOSED =  1,   /* peer closed the connection cleanly between frames   */
    FRAME_ERR    = -1    /* I/O error, truncated frame, or invalid length       */
};

/* Read exactly n bytes. Returns 1 when all n bytes were read,
 * 0 when the peer closed before the first byte (clean end),
 * -1 on error or when the peer closed after some of the bytes. */
int read_exact(int fd, void *buf, size_t n);

/* Write exactly n bytes. Returns 1 on success, -1 on error. */
int write_all(int fd, const void *buf, size_t n);

/* Read one frame: 2-byte big-endian length, then that many bytes.
 * On FRAME_OK, *len holds the message length (header not included). */
int read_frame(int fd, unsigned char *buf, size_t cap, size_t *len);

/* Write one frame: 2-byte big-endian length, then the message.
 * Returns 0 on success, -1 on error. */
int write_frame(int fd, const unsigned char *msg, size_t len);

/* Debug aid (implemented): print bytes in hex to stderr. */
void hex_dump(const char *label, const unsigned char *buf, size_t len);

#endif
