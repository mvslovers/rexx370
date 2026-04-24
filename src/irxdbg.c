/* ------------------------------------------------------------------ */
/*  irxdbg.c - REXX/370 Debug / Inspection Tool                       */
/*                                                                    */
/*  Subcommands (case-insensitive):                                    */
/*    ANCH   - show IRXANCHR registry state                            */
/*    CLEAR  - reset IRXANCHR to initial state                         */
/*    ECT    - show ECT and ECTENVBK of current TCB                    */
/*    ENV    - show ENVBLOCK [at address], default: follow ECTENVBK    */
/*                                                                    */
/*  Invocation (TSO):                                                  */
/*    CALL 'hlq.LOAD(IRXDBG)' 'ANCH'                                   */
/*    CALL 'hlq.LOAD(IRXDBG)' 'ENV 18BC5C90'                           */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                             */
/* ------------------------------------------------------------------ */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irx.h"
#include "irxanchr.h"

#ifndef __MVS__
/* Required by irx#anch.c host simulation (ectenvbk_slot) */
void *_simulated_ectenvbk = NULL;
#endif

/* PSA+0x21C: 24-bit pointer to the current TCB (IBM field PSATOLD). */
#define PSA_PSATOLD 0x21C

/* Number of raw bytes to dump when the ENVBLOCK eye-catcher is invalid. */
#define RAW_DUMP_BYTES 16

/* Maximum length (including NUL) of a subcommand name buffer. */
#define SUBCMD_BUFLEN 16

/* Hex base for strtoul when parsing an address argument. */
#define HEX_BASE 16

/* Tool return codes (MVS convention). */
enum
{
    RC_OK = 0,
    RC_WARN = 4,
    RC_ERROR = 8
};

/* ================================================================== */
/*  Dispatch table                                                     */
/* ================================================================== */

typedef int (*subcmd_fn_t)(int argc, char **argv);

static int cmd_anch(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_ect(int argc, char **argv);
static int cmd_env(int argc, char **argv);

static const struct
{
    const char *name;
    subcmd_fn_t fn;
    const char *help;
} commands[] = {
    {"ANCH", cmd_anch, "show IRXANCHR registry state"},
    {"CLEAR", cmd_clear, "reset IRXANCHR to initial state"},
    {"ECT", cmd_ect, "show ECT and ECTENVBK of current TCB"},
    {"ENV", cmd_env, "show ENVBLOCK [at address], default: follow ECTENVBK"},
    {NULL, NULL, NULL}};

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

static void str_upper(char *s)
{
    for (; *s; s++)
    {
        *s = (char)toupper((unsigned char)*s);
    }
}

static void print_usage(void)
{
    int i;

    printf("Usage: IRXDBG subcommand [args]\n\n");
    printf("Subcommands:\n");
    for (i = 0; commands[i].name != NULL; i++)
    {
        printf("  %-8s  %s\n", commands[i].name, commands[i].help);
    }
    printf("\nExamples:\n");
    printf("  CALL 'hlq.LOAD(IRXDBG)' 'ANCH'\n");
    printf("  CALL 'hlq.LOAD(IRXDBG)' 'ENV 18BC5C90'\n");
}

/* Map a 4-byte ENVBLOCK version field to a human-readable label. */
static const char *version_label(const unsigned char *ver4)
{
    /* sizeof("0042") - 1 == 4, giving the version field width at compile time */
    if (memcmp(ver4, IRXANCHR_VERSION, sizeof(IRXANCHR_VERSION) - 1) == 0)
    {
        return "(rexx370)";
    }
    if (memcmp(ver4, ENVBLOCK_VERSION_0100, sizeof(ENVBLOCK_VERSION_0100) - 1) == 0)
    {
        return "(IBM TSO/E v2)";
    }
    return "(unknown)";
}

/* Read the 4-byte token counter from the IRXANCHR header reserved field. */
static uint32_t read_token_counter(const irxanchr_header_t *hdr)
{
    uint32_t val;
    memcpy(&val, hdr->reserved, sizeof(val));
    return val;
}

/* ================================================================== */
/*  ANCH                                                               */
/* ================================================================== */

static int cmd_anch(int argc, char **argv)
{
    irxanchr_header_t *hdr;
    int rc;

    (void)argc;
    (void)argv;

    rc = irx_anchor_get_handle(&hdr);
    if (rc != IRX_ANCHOR_RC_OK)
    {
        printf("IRXANCHR: get_handle failed (RC=%d)\n", rc);
        return RC_ERROR;
    }

    irxanchr_entry_t *slots = (irxanchr_entry_t *)((char *)hdr + sizeof(irxanchr_header_t));

    printf("IRXANCHR @ %08X\n", (unsigned)(unsigned long)hdr);
    printf("Eye-catcher:    '%.8s'\n", hdr->id);
    printf("Version:        '%.4s'\n", hdr->version);
    printf("TOTAL:          %u\n", (unsigned)hdr->total);
    printf("USED:           %u\n", (unsigned)hdr->used);
    printf("Token counter:  %u\n", (unsigned)read_token_counter(hdr));
    printf("LENGTH:         %u\n", (unsigned)hdr->length);
    printf("Permanent sentinels: slots 0, 2\n\n");

    printf("%-4s | %-12s | %-7s | %-8s | %-8s | %-8s\n",
           "Slot", "envblock_ptr", "token", "tcb_ptr", "flags", "hint");
    printf("------+--------------+-------+----------+----------+----------\n");

    int active = 0;

    for (uint32_t i = 0; i < hdr->used; i++)
    {
        if (slots[i].envblock_ptr == IRXANCHR_SLOT_FREE ||
            slots[i].envblock_ptr == IRXANCHR_SLOT_SENTINEL)
        {
            continue;
        }
        active++;
        printf("%4u | %12X | %7u | %8X | %8X | %8X\n",
               (unsigned)i,
               (unsigned)slots[i].envblock_ptr,
               (unsigned)slots[i].token,
               (unsigned)slots[i].tcb_ptr,
               (unsigned)slots[i].flags,
               (unsigned)slots[i].anchor_hint);
    }

    printf("\nActive entries: %d / Free slots: %u\n",
           active, (unsigned)(hdr->total - hdr->used));

    return RC_OK;
}

/* ================================================================== */
/*  CLEAR                                                              */
/* ================================================================== */

static int cmd_clear(int argc, char **argv)
{
    irxanchr_header_t *hdr;
    int rc;

    (void)argc;
    (void)argv;

    irx_anchor_table_reset();

    rc = irx_anchor_get_handle(&hdr);
    if (rc != IRX_ANCHOR_RC_OK)
    {
        printf("IRXANCHR reset: get_handle failed after reset (RC=%d)\n", rc);
        return RC_ERROR;
    }

    printf("IRXANCHR reset: USED=%u, token_counter=%u, sentinels restored\n",
           (unsigned)hdr->used, (unsigned)read_token_counter(hdr));

    return RC_OK;
}

/* ================================================================== */
/*  ECT                                                                */
/* ================================================================== */

static int cmd_ect(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifdef __MVS__
    void *tcb = *(void **)PSA_PSATOLD;
    void *ect = anch_walk();
    struct envblock *eb;

    printf("Current TCB (PSATOLD):  %08X\n", (unsigned)(unsigned long)tcb);

    if (ect == NULL)
    {
        printf("Current ECT:            (not reachable -- pure batch or LWA NULL)\n");
        return RC_OK;
    }

    printf("Current ECT:            %08X\n", (unsigned)(unsigned long)ect);

    /* Use anch_curr() — it reads ECT+0x30 via the platform abstraction
     * in irx#anch.c rather than computing the offset here. */
    eb = anch_curr();
    printf("ECTENVBK (ECT+0x30):    %08X\n", (unsigned)(unsigned long)eb);

    if (eb == NULL)
    {
        printf("ENVBLOCK:               (ECTENVBK = 00000000, no REXX env active)\n");
        return RC_OK;
    }

    printf("ENVBLOCK @ %08X:\n", (unsigned)(unsigned long)eb);

    if (memcmp(eb->envblock_id, ENVBLOCK_ID, sizeof(eb->envblock_id)) != 0)
    {
        printf("  ** eye-catcher mismatch -- not a valid ENVBLOCK **\n");
        return RC_OK;
    }

    printf("  Eye-catcher:  '%.8s'     (valid)\n", (char *)eb->envblock_id);
    printf("  Version:      '%.4s'         %s\n",
           (char *)eb->envblock_version,
           version_label(eb->envblock_version));
    printf("  Length:       %d\n", eb->envblock_length);
#else
    printf("Current TCB (PSATOLD):  (not available on host)\n");
    printf("Current ECT:            (not available on host)\n");
    printf("Note: ECT/TCB inspection requires MVS 3.8j\n");
#endif

    return RC_OK;
}

/* ================================================================== */
/*  ENV                                                                */
/* ================================================================== */

static void print_env(struct envblock *eb)
{
    int i;

    printf("ENVBLOCK @ %08X\n", (unsigned)(unsigned long)eb);

    if (memcmp(eb->envblock_id, ENVBLOCK_ID, sizeof(eb->envblock_id)) != 0)
    {
        unsigned char *raw = (unsigned char *)eb;
        printf("** not a valid ENVBLOCK **\n");
        printf("First %d bytes:", RAW_DUMP_BYTES);
        for (i = 0; i < RAW_DUMP_BYTES; i++)
        {
            printf(" %02X", (unsigned)raw[i]);
        }
        printf("\n");
        return;
    }

    int msgid_empty = 1;

    for (i = 0; i < (int)sizeof(eb->error_msgid); i++)
    {
        if (eb->error_msgid[i] != ' ' && eb->error_msgid[i] != '\0')
        {
            msgid_empty = 0;
            break;
        }
    }

    printf("+0x00  id:           '%.8s'\n", (char *)eb->envblock_id);
    printf("+0x08  version:      '%.4s'  %s\n",
           (char *)eb->envblock_version,
           version_label(eb->envblock_version));
    printf("+0x0C  length:       %d (0x%X)\n",
           eb->envblock_length, (unsigned)eb->envblock_length);
    printf("+0x10  parmblock:    %08X\n", (unsigned)(unsigned long)eb->envblock_parmblock);
    printf("+0x14  userfield:    %08X\n", (unsigned)(unsigned long)eb->envblock_userfield);
    printf("+0x18  workblkext:   %08X\n", (unsigned)(unsigned long)eb->envblock_workblok_ext);
    printf("+0x1C  irxexte:      %08X\n", (unsigned)(unsigned long)eb->envblock_irxexte);
    printf("+0x20  error_call:   %08X\n", (unsigned)(unsigned long)eb->error_call_);

    if (msgid_empty)
    {
        printf("+0x28  error_msgid:  (empty)\n");
    }
    else
    {
        printf("+0x28  error_msgid:  '%.8s'\n", (char *)eb->error_msgid);
    }

    printf("+0x120 compgmtb:     %08X\n", (unsigned)(unsigned long)eb->envblock_compgmtb);
    printf("+0x124 attnrout:     %08X\n", (unsigned)(unsigned long)eb->envblock_attnrout_parmptr);
    printf("+0x128 ectptr:       %08X\n", (unsigned)(unsigned long)eb->envblock_ectptr);

    printf("+0x12C info_flags:   ");
    for (i = 0; i < (int)sizeof(eb->envblock_info_flags); i++)
    {
        printf("%02X", (unsigned)eb->envblock_info_flags[i]);
    }
    printf("\n");

    printf("+0x130 reserved:     (16 bytes, rexx370-private)\n");
}

static int cmd_env(int argc, char **argv)
{
    struct envblock *eb;

    if (argc == 0)
    {
        eb = anch_curr();
        if (eb == NULL)
        {
            printf("ECTENVBK = 00000000: no REXX environment active\n");
            return RC_OK;
        }
    }
    else
    {
        const char *arg = argv[0];
        char *endp;

        /* Accept optional 0x / 0X prefix */
        if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
        {
            arg += 2;
        }

        if (*arg == '\0')
        {
            printf("ENV: empty address argument\n");
            return RC_WARN;
        }

        uint32_t addr = (uint32_t)strtoul(arg, &endp, HEX_BASE);

        if (*endp != '\0')
        {
            printf("ENV: invalid hex address '%s'\n", argv[0]);
            return RC_WARN;
        }

        if (addr == 0)
        {
            printf("ENV: address 00000000 is NULL\n");
            return RC_WARN;
        }

        eb = (struct envblock *)(unsigned long)addr;
    }

    print_env(eb);
    return RC_OK;
}

/* ================================================================== */
/*  Entry point                                                        */
/* ================================================================== */

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage();
        return RC_OK;
    }

    char subcmd[SUBCMD_BUFLEN];

    strncpy(subcmd, argv[1], sizeof(subcmd) - 1);
    subcmd[sizeof(subcmd) - 1] = '\0';
    str_upper(subcmd);

    for (int i = 0; commands[i].name != NULL; i++)
    {
        if (strcmp(subcmd, commands[i].name) == 0)
        {
            return commands[i].fn(argc - 2, argv + 2);
        }
    }

    printf("IRXDBG: unknown subcommand '%s'\n\n", argv[1]);
    print_usage();
    return RC_WARN;
}
