#ifndef HANDLER_H
#define HANDLER_H

#include "sim_iso8583.h"

/* Business rules. Given a parsed request, fill in the response.
 *
 * Returns 0  : *resp is ready, send it.
 * Returns -1 : the request cannot be answered (for example an MTI the
 *              server does not support). The caller drops the connection
 *              and sends nothing.
 */
int handle_message(const iso_msg *req, iso_msg *resp);

#endif
