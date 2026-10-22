#ifndef SIM_ISO8583_H
#define SIM_ISO8583_H

#include <stddef.h>

/* Limits of this simulator (not limits of the ISO 8583 standard). */
#define ISO_MAX_MSG    1024   /* max message size in bytes, without the 2-byte length header */
#define ISO_MAX_FIELD  64     /* only the primary bitmap (fields 1-64) is supported          */
#define ISO_FIELD_LEN  32     /* storage per field value, including the terminating '\0'    */

/* Return codes. 0 means success, negative values are errors. */
enum {
    ISO_OK                 =  0,
    ISO_ERR_SHORT          = -1,  /* message is shorter than its own structure requires */
    ISO_ERR_BAD_MTI        = -2,  /* MTI is not 4 ASCII digits                          */
    ISO_ERR_SECONDARY      = -3,  /* bit 1 set: secondary bitmap is not supported       */
    ISO_ERR_UNKNOWN_FIELD  = -4,  /* bitmap marks a field that is not in the spec table */
    ISO_ERR_BAD_FIELD      = -5,  /* bad length prefix, non-digit, or bytes left over   */
    ISO_ERR_OVERFLOW       = -6,  /* output buffer or field storage too small           */
    ISO_ERR_NOT_IMPL       = -99  /* placeholder: you still have to write this function */
};

/* How the length of a field is encoded on the wire. */
typedef enum {
    ISO_FIXED,   /* always exactly max_len bytes                                    */
    ISO_LLVAR    /* 2 ASCII digits give the length, then that many bytes of value   */
} iso_len_kind;

/* One row of the field table (the list of fields this simulator knows). */
typedef struct {
    int          field;        /* field number, 2..64                          */
    const char  *name;         /* human-readable name for log messages         */
    iso_len_kind kind;
    int          max_len;      /* exact length (ISO_FIXED) or maximum (LLVAR)  */
    int          digits_only;  /* 1 = value may contain only characters 0-9    */
} iso_field_spec;

/* Parsed representation of one message. */
typedef struct {
    char mti[5];                                            /* e.g. "0200" plus '\0'         */
    int  present[ISO_MAX_FIELD + 1];                        /* present[n] != 0 if field n set */
    char value[ISO_MAX_FIELD + 1][ISO_FIELD_LEN];           /* value[n]: field n as text      */
} iso_msg;

/* Helpers (already implemented in sim_iso8583.c). */
void                  iso_init(iso_msg *m);                          /* clear everything              */
const iso_field_spec *iso_spec(int field);                           /* NULL if field not in table    */
int                   iso_set_mti(iso_msg *m, const char *mti);      /* ISO_OK or ISO_ERR_BAD_MTI     */
int                   iso_set(iso_msg *m, int field, const char *text); /* checks against the table   */
const char           *iso_get(const iso_msg *m, int field);          /* NULL if field not present     */

/* To implement (see the TODO comments in sim_iso8583.c). */
int iso_parse(const unsigned char *buf, size_t len, iso_msg *out);   /* ISO_OK or ISO_ERR_*           */
int iso_build(const iso_msg *m, unsigned char *out, size_t cap);     /* bytes written, or ISO_ERR_*   */

#endif
