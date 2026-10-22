#include <stdio.h>
#include <string.h>
#include <time.h>
#include "handler.h"

/* Business constants. */
#define LIMIT_MINOR_UNITS   500000ULL            /* 5000.00: above this -> code 61 */
#define TEST_PAN_APPROVE    "4111111111111111"   /* normal approval                */
#define TEST_PAN_DECLINE    "4000000000000002"   /* always code 05                 */
#define TEST_PAN_NO_FUNDS   "4000000000009995"   /* always code 51                 */

/* ------------------------------------------------------------------ */
/* TODO: handle_message                                               */
/*                                                                    */
/* Full description with a decision table: ARCHITECTURE.md section 7. */
/*                                                                    */
/* Suggested helper functions (write them as static functions):       */
/*   static int luhn_ok(const char *pan);                             */
/*       Luhn check: from the right, double every second digit, if    */
/*       the result is above 9 subtract 9, add all digits, the sum    */
/*       must be divisible by 10. Also require 13..19 digits.         */
/*   static int has_all(const iso_msg *req, const int *fields, int n);*/
/*       1 if every listed field number is present in req.            */
/*   static void make_rrn(const iso_msg *req, char out[13]);          */
/*       12 characters, for example 6 digits from time(NULL) % 10^6   */
/*       followed by the 6 digits of the STAN (field 11).             */
/*                                                                    */
/* Steps:                                                             */
/*  1. MTI "0800": resp = copy of req with MTI "0810" and field 39    */
/*     "00". Return 0.                                                */
/*  2. MTI "0200": go to step 3.                                      */
/*  3. Any other MTI: return -1 (the connection will be dropped).     */
/*  4. resp = copy of req, MTI "0210". Then pick the response code    */
/*     with the first rule that matches, in this order:               */
/*        required fields 2,3,4,7,11,41 not all present  -> "30"      */
/*        processing code is not "000000"                -> "12"      */
/*        PAN fails luhn_ok                              -> "14"      */
/*        amount == 0                                    -> "13"      */
/*        amount > LIMIT_MINOR_UNITS                     -> "61"      */
/*        PAN == TEST_PAN_DECLINE                        -> "05"      */
/*        PAN == TEST_PAN_NO_FUNDS                       -> "51"      */
/*        otherwise                                      -> "00"      */
/*  5. iso_set(resp, 39, code). If the code is "00", also             */
/*     iso_set(resp, 37, rrn). Return 0.                              */
/*                                                                    */
/* Amount: field 4 has up to 12 digits (999,999,999,999). That does   */
/* not fit in a 32-bit long. Convert with strtoull into an            */
/* unsigned long long (Day 1 legacy: use long and atol).              */
/* ------------------------------------------------------------------ */
int handle_message(const iso_msg *req, iso_msg *resp)
{
    (void)req;
    (void)resp;
    return -1;
}
