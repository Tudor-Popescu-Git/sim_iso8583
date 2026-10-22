#include <string.h>
#include "sim_iso8583.h"

/* ------------------------------------------------------------------ */
/* Field table: the only fields this simulator understands.           */
/* To support another field, add one row here. Nothing else changes. */
/* ------------------------------------------------------------------ */
static const iso_field_spec SPEC[] = {
    /* no  name                      kind       len  digits */
    {  2, "PAN",                    ISO_LLVAR,  19,  1 },
    {  3, "Processing code",        ISO_FIXED,   6,  1 },
    {  4, "Amount (minor units)",   ISO_FIXED,  12,  1 },
    {  7, "Transmission date/time", ISO_FIXED,  10,  1 },   /* MMDDhhmmss */
    { 11, "STAN",                   ISO_FIXED,   6,  1 },
    { 37, "Retrieval reference",    ISO_FIXED,  12,  0 },
    { 39, "Response code",          ISO_FIXED,   2,  0 },
    { 41, "Terminal ID",            ISO_FIXED,   8,  0 }
};
#define SPEC_COUNT (sizeof SPEC / sizeof SPEC[0])

/* ------------------------------------------------------------------ */
/* Helpers (implemented).                                             */
/* ------------------------------------------------------------------ */

void iso_init(iso_msg *m)
{
    memset(m, 0, sizeof *m);
}

const iso_field_spec *iso_spec(int field)
{
    size_t i;
    for (i = 0; i < SPEC_COUNT; i++) {
        if (SPEC[i].field == field)
            return &SPEC[i];
    }
    return NULL;
}

int iso_set_mti(iso_msg *m, const char *mti)
{
    int i;
    if (strlen(mti) != 4)
        return ISO_ERR_BAD_MTI;
    for (i = 0; i < 4; i++) {
        if (mti[i] < '0' || mti[i] > '9')
            return ISO_ERR_BAD_MTI;
    }
    memcpy(m->mti, mti, 5);   /* 4 digits + '\0' */
    return ISO_OK;
}

int iso_set(iso_msg *m, int field, const char *text)
{
    const iso_field_spec *sp = iso_spec(field);
    size_t n = strlen(text);
    size_t i;

    if (sp == NULL)
        return ISO_ERR_UNKNOWN_FIELD;
    if (sp->kind == ISO_FIXED && n != (size_t)sp->max_len)
        return ISO_ERR_BAD_FIELD;
    if (sp->kind == ISO_LLVAR && (n < 1 || n > (size_t)sp->max_len))
        return ISO_ERR_BAD_FIELD;
    if (n >= ISO_FIELD_LEN)
        return ISO_ERR_OVERFLOW;
    if (sp->digits_only) {
        for (i = 0; i < n; i++) {
            if (text[i] < '0' || text[i] > '9')
                return ISO_ERR_BAD_FIELD;
        }
    }
    memcpy(m->value[field], text, n + 1);
    m->present[field] = 1;
    return ISO_OK;
}

const char *iso_get(const iso_msg *m, int field)
{
    if (field < 1 || field > ISO_MAX_FIELD || !m->present[field])
        return NULL;
    return m->value[field];
}

/* ------------------------------------------------------------------ */
/* TODO: iso_parse                                                    */
/*                                                                    */
/* Wire layout of one message (see docs/ARCHITECTURE.md section 6):   */
/*   bytes 0-3    MTI, 4 ASCII digits                                 */
/*   bytes 4-11   primary bitmap, 8 raw bytes                         */
/*   bytes 12...  the fields whose bit is set, in ascending order     */
/*                                                                    */
/* Steps:                                                             */
/*  1. iso_init(out). If len < 12 return ISO_ERR_SHORT.               */
/*  2. MTI: check 4 digits, copy into out->mti, add '\0'.             */
/*     Not digits -> ISO_ERR_BAD_MTI.                                 */
/*  3. Bitmap: if bit 1 is set (buf[4] & 0x80) -> ISO_ERR_SECONDARY.  */
/*  4. For n = 2 .. 64, in this order:                                */
/*       bit n is set when  buf[4 + (n-1)/8] & (0x80 >> ((n-1)%8))    */
/*       - not set: continue.                                         */
/*       - iso_spec(n) == NULL -> ISO_ERR_UNKNOWN_FIELD.              */
/*       - ISO_FIXED: need max_len bytes left, else ISO_ERR_BAD_FIELD.*/
/*       - ISO_LLVAR: need 2 bytes left, both digits, value <= max_len*/
/*         and that many bytes left, else ISO_ERR_BAD_FIELD.          */
/*       - digits_only: every byte must be '0'..'9'.                  */
/*       - copy the value into out->value[n], add '\0',               */
/*         set out->present[n] = 1, move the offset forward.          */
/*  5. After the last field, the offset must equal len.               */
/*     Bytes left over -> ISO_ERR_BAD_FIELD.                          */
/*                                                                    */
/* LEGACY STYLE (Day 1, deliberate - see ARCHITECTURE.md section 11): */
/*   Test bitmap bits by copying the 8 bytes into an unsigned long    */
/*   long and using (1ULL << (64 - n)). Use long for the amount.      */
/*   It looks correct, works on big-endian CPUs, and fails on x86.    */
/* PORTABLE STYLE (Day 3): test single bytes as in step 4, use        */
/*   uint64_t for the amount, no casts onto the buffer.               */
/* ------------------------------------------------------------------ */
int iso_parse(const unsigned char *buf, size_t len, iso_msg *out)
{
    (void)buf;
    (void)len;
    (void)out;
    return ISO_ERR_NOT_IMPL;
}

/* ------------------------------------------------------------------ */
/* TODO: iso_build                                                    */
/*                                                                    */
/* Steps:                                                             */
/*  1. Check cap >= 12 (MTI + bitmap) else ISO_ERR_OVERFLOW.          */
/*  2. Write m->mti (4 bytes). Check it is 4 digits.                  */
/*  3. Clear the 8 bitmap bytes. For n = 2 .. 64 with present[n] set, */
/*     set bit n:  bitmap[(n-1)/8] |= 0x80 >> ((n-1)%8)               */
/*  4. Append each present field in ascending order:                  */
/*       ISO_FIXED: the value bytes (length must equal max_len).      */
/*       ISO_LLVAR: two ASCII digits with the length (for example     */
/*                  "16"), then the value bytes.                      */
/*     Before every write, check there is space in out (cap).         */
/*  5. Return the number of bytes written.                            */
/*     Do NOT write the 2-byte length header here. The framing layer  */
/*     does that (framing.c).                                         */
/* ------------------------------------------------------------------ */
int iso_build(const iso_msg *m, unsigned char *out, size_t cap)
{
    (void)m;
    (void)out;
    (void)cap;
    return ISO_ERR_NOT_IMPL;
}
