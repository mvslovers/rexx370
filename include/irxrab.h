/* ------------------------------------------------------------------ */
/*  irxrab.h - REXX Anchor Block (RAB) and Environment Node           */
/*                                                                    */
/*  The RAB anchors all REXX/370 environments for a given task.       */
/*  It is pointed to by TCBUSER and maintains a doubly linked list    */
/*  of irx_env_node wrappers around the IBM-compatible ENVBLOCKs.     */
/*                                                                    */
/*  Chain topology:                                                   */
/*                                                                    */
/*  TCB -> TCBUSER -> RAB -> env_node <-> env_node <-> ...            */
/*                            |              |                        */
/*                            v              v                        */
/*                          ENVBLOCK       ENVBLOCK   (IBM layout)    */
/*                            |              |                        */
/*                            v              v                        */
/*                        irx_wkblk_int  irx_wkblk_int  (ours)       */
/*                                                                    */
/*  The ENVBLOCK itself stays IBM-compatible. Our internal runtime    */
/*  state lives in irx_wkblk_int, pointed to by envblock_userfield.  */
/*                                                                    */
/*  This is NOT an IBM-defined control block.                         */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXRAB_H
#define IRXRAB_H

#include "irx.h"

/* ================================================================== */
/*  REXX Anchor Block (RAB)                                           */
/*  Anchored via TCBUSER on MVS 3.8j                                  */
/* ================================================================== */

struct irx_rab
{
    unsigned char rab_id[4]; /* Eye-catcher: 'RAB '            */
    int rab_length;          /* Length of this block            */
    short int rab_version;   /* Version number                 */
    short int rab_env_count; /* Number of active environments  */
    void *rab_first;         /* -> first irx_env_node          */
    void *rab_last;          /* -> last irx_env_node           */
    void *rab_prev_tcbuser;  /* Saved previous TCBUSER value   */
    int rab_flags;           /* RAB status flags               */
    int _reserved[2];        /* reserved for future use        */
};

#define RAB_ID      "RAB "
#define RAB_VERSION 1

/* RAB flags */
#define RAB_ACTIVE 0x80000000 /* RAB is initialized and active    */
#define RAB_TSO    0x40000000 /* Running under TSO                */

/* ================================================================== */
/*  Environment Node (irx_env_node)                                   */
/*  Wraps ENVBLOCK for chain navigation without modifying IBM layout  */
/* ================================================================== */

struct irx_env_node
{
    unsigned char node_id[4];       /* Eye-catcher: 'ENVN'            */
    int node_length;                /* Length of this node             */
    struct irx_env_node *node_next; /* -> next environment node       */
    struct irx_env_node *node_prev; /* -> previous environment node   */
    struct irx_rab *node_rab;       /* -> owning RAB                  */
    struct envblock *node_envblock; /* -> the IBM ENVBLOCK            */
    int node_flags;                 /* Node status flags              */
    int _reserved;                  /* reserved                       */
};

#define ENVNODE_ID "ENVN"

#define ENVNODE_ACTIVE 0x80000000 /* Environment is active             */
#define ENVNODE_TERM   0x40000000 /* Being terminated                  */

#endif /* IRXRAB_H */
