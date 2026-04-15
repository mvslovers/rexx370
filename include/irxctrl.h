/* ------------------------------------------------------------------ */
/*  irxctrl.h - REXX/370 Control Flow Infrastructure (WP-15)          */
/*                                                                    */
/*  Label table and execution stack for DO/IF/SELECT/CALL/RETURN/EXIT */
/*  SIGNAL, NOP, ITERATE, LEAVE. All memory goes through the          */
/*  injected lstring370 allocator.                                     */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 8 (Instructions)                        */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXCTRL_H
#define IRXCTRL_H

#include "lstralloc.h"
#include "lstring.h"

/* ================================================================== */
/*  Forward declarations                                              */
/* ================================================================== */

struct irx_parser;
struct irx_vpool; /* used by FRAME_CALL for PROCEDURE EXPOSE (WP-17) */

/* ================================================================== */
/*  Frame type codes                                                  */
/* ================================================================== */

#define FRAME_DO     1
#define FRAME_CALL   2
#define FRAME_SELECT 3

/* ================================================================== */
/*  DO loop variant codes                                             */
/* ================================================================== */

#define DO_SIMPLE  0 /* DO; ... END  (execute body once)           */
#define DO_FOREVER 1 /* DO FOREVER                                 */
#define DO_COUNT   2 /* DO n  (fixed-count repetitive)             */
#define DO_CTRL    3 /* DO i = start TO end [BY step]              */
#define DO_WHILE   4 /* DO WHILE cond  (pre-test)                  */
#define DO_UNTIL   5 /* DO UNTIL cond  (post-test)                 */

/* Maximum name length for a control variable or DO label. */
#define CTRL_NAME_MAX 64

/* ================================================================== */
/*  Label table                                                       */
/* ================================================================== */

struct irx_label
{
    char name[CTRL_NAME_MAX]; /* upper-case label name (no colon)   */
    int name_len;
    int tok_pos; /* token position of the label symbol  */
};

struct irx_label_table
{
    struct irx_label *entries;
    int count;
    int cap;
    struct lstr_alloc *alloc;
};

/* ================================================================== */
/*  Execution frame                                                   */
/* ================================================================== */

struct irx_exec_frame
{
    int frame_type; /* FRAME_DO / FRAME_CALL / FRAME_SELECT       */

    /* --- FRAME_DO fields ----------------------------------------- */
    int do_type;    /* DO_SIMPLE / DO_FOREVER / DO_COUNT / ...    */
    int loop_start; /* tok_pos of first body token                */
    int loop_end;   /* tok_pos of first token AFTER matching END  */

    /* Controlled loop (DO_CTRL) */
    char ctrl_name[CTRL_NAME_MAX];
    int ctrl_name_len;
    long ctrl_val; /* current value of the control variable      */
    long ctrl_to;  /* limit (inclusive)                          */
    long ctrl_by;  /* step (default 1)                           */

    /* Repetitive loop (DO_COUNT) */
    long ctrl_count; /* remaining iterations                       */

    /* Conditional loops (DO_WHILE / DO_UNTIL) */
    int cond_tok_pos; /* tok_pos of the WHILE/UNTIL expression      */

    /* Label associated with this DO (for ITERATE/LEAVE label) */
    char do_label[CTRL_NAME_MAX];
    int do_label_len;

    /* First iteration flag for DO_UNTIL (body must execute once) */
    int first_iter;

    /* --- FRAME_CALL fields --------------------------------------- */
    int call_return_pos; /* tok_pos to resume after RETURN         */
    int call_line;       /* source line of the CALL (-> SIGL)      */

    /* WP-17: PROCEDURE EXPOSE */
    struct irx_vpool *saved_vpool; /* caller's vpool before PROCEDURE   */
    Lstr *saved_args;              /* caller's call_args array           */
    int *saved_arg_exists;         /* caller's call_arg_exists array     */
    int saved_argc;                /* caller's call_argc                 */
    int procedure_allowed;         /* 1 = PROCEDURE may follow next    */
    int has_procedure;             /* 1 = PROCEDURE was executed         */

    /* --- FRAME_SELECT fields ------------------------------------- */
    int select_matched; /* 1 once a WHEN branch has been taken    */
    int select_end;     /* tok_pos of first token AFTER SELECT END */
};

/* ================================================================== */
/*  Execution stack                                                   */
/* ================================================================== */

#define EXEC_STACK_INIT_CAP 16

struct irx_exec_stack
{
    struct irx_exec_frame *frames;
    int top; /* number of active frames        */
    int cap; /* allocated frame slots          */
    struct lstr_alloc *alloc;

    /* "Last label seen" — cleared by kw_do after pick-up. Used to
     * associate a label clause with the immediately following DO. */
    char last_label[CTRL_NAME_MAX];
    int last_label_len;

    /* EXIT/RETURN state propagated upward through kw_return. */
    int exit_requested;
    int exit_rc;
};

/* ================================================================== */
/*  Public entry points                                               */
/*                                                                    */
/*  asm() aliases are required because all irx_ctrl_* functions share */
/*  the same first 8 characters ("irx_ctrl") and would collide under  */
/*  c2asm370's 8-character identifier truncation, leaving only the    */
/*  first non-static function visible as the module entry point.      */
/* ================================================================== */

/* Allocate and attach a label table and exec stack to the parser.
 * Must be called from irx_pars_init before any parsing starts. */
int irx_ctrl_init(struct irx_parser *p) asm("IRXCTLIN");

/* Free everything attached by irx_ctrl_init. Safe to call on a
 * partially initialised parser. */
void irx_ctrl_cleanup(struct irx_parser *p) asm("IRXCTLCL");

/* First-pass scan of the token stream. Populates the label table
 * so that SIGNAL/CALL can resolve names without forward-lookup. */
int irx_ctrl_label_scan(struct irx_parser *p) asm("IRXCTLLS");

/* Look up a label by upper-case name. Returns tok_pos (>= 0) or -1. */
int irx_ctrl_label_find(struct irx_parser *p,
                        const char *name,
                        int name_len) asm("IRXCTLLF");

/* Push a new frame. Returns pointer to the frame (never NULL on
 * success) or NULL on allocator failure. */
struct irx_exec_frame *irx_ctrl_frame_push(
    struct irx_parser *p,
    int frame_type) asm("IRXCTLFP");

/* Return pointer to the topmost frame, or NULL if the stack is empty. */
struct irx_exec_frame *irx_ctrl_frame_top(
    struct irx_parser *p) asm("IRXCTLFT");

/* Pop the topmost frame. No-op if stack is empty. */
void irx_ctrl_frame_pop(struct irx_parser *p) asm("IRXCTLFO");

/* Find the innermost DO frame, optionally matching a specific label.
 * label/len may be NULL/0 to find the innermost DO regardless of label.
 * Returns frame index (>= 0) or -1 if not found. */
int irx_ctrl_find_do(struct irx_parser *p,
                     const char *label,
                     int label_len) asm("IRXCTLFD");

#endif /* IRXCTRL_H */
