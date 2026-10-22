#include <stdio.h>
#include "connection.h"
#include "framing.h"
#include "sim_iso8583.h"
#include "handler.h"

/* ------------------------------------------------------------------ */
/* TODO: serve_client                                                 */
/*                                                                    */
/* Local variables you will need:                                     */
/*   unsigned char rx[ISO_MAX_MSG], tx[ISO_MAX_MSG];                  */
/*   size_t rx_len; int tx_len; iso_msg req, resp;                    */
/*                                                                    */
/* Loop until the client closes the connection:                       */
/*   a. rc = read_frame(fd, rx, sizeof rx, &rx_len)                   */
/*        FRAME_CLOSED -> normal end, return 0                        */
/*        FRAME_ERR    -> print a message to stderr, return -1        */
/*   b. hex_dump("rx", rx, rx_len)                    (debug aid)     */
/*   c. rc = iso_parse(rx, rx_len, &req)                              */
/*        rc < 0 -> print the error code, return -1.                  */
/*        No reply is sent for a message that cannot be parsed.       */
/*   d. rc = handle_message(&req, &resp)                              */
/*        rc < 0 -> print a message, return -1 (no reply).            */
/*   e. tx_len = iso_build(&resp, tx, sizeof tx)                      */
/*        tx_len < 0 -> print a message, return -1.                   */
/*   f. hex_dump("tx", tx, (size_t)tx_len)                            */
/*      write_frame(fd, tx, (size_t)tx_len)  (-1 -> return -1)        */
/*   g. Print one log line to stderr: MTI, STAN, response code.       */
/*      Day 4: this is where the record goes to the logger process.   */
/*                                                                    */
/* Why one function per connection and a loop: a client may send      */
/* many requests on the same connection. The process lives as long    */
/* as the connection does.                                            */
/* ------------------------------------------------------------------ */
int serve_client(int fd)
{
    (void)fd;
    fprintf(stderr, "serve_client: not implemented yet\n");
    return -1;
}
