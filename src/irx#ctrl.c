/* ------------------------------------------------------------------ */
/*  irxctrl.c - REXX/370 Control Flow Infrastructure (WP-15)          */
/*                                                                    */
/*  Implements the label table and execution stack that support       */
/*  DO/IF/SELECT/CALL/RETURN/EXIT/SIGNAL/ITERATE/LEAVE.              */
/*                                                                    */
/*  All memory goes through the injected lstring370 allocator (the    */
/*  same allocator as irx_parser.alloc). irxstor is not used here     */
/*  because the envblock may be NULL in cross-compile unit tests.     */
/*                                                                    */
/*  Ref: SC28-1883-0, Chapter 8 (Instructions)                        */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#include <string.h>
#include <stddef.h>
#include <ctype.h>

#include "irxpars.h"
#include "irxctrl.h"
#include "irxtokn.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/* Upper-case at most CTRL_NAME_MAX-1 bytes from tok_text into dst.
 * Returns the number of bytes written. */
static int tok_to_upper_name(const struct irx_token *t,
                              char *dst, int dst_max)
{
    int n = (int)t->tok_length;
    int i;
    if (n >= dst_max) n = dst_max - 1;
    for (i = 0; i < n; i++) {
        int c = (unsigned char)t->tok_text[i];
        dst[i] = (char)(islower(c) ? toupper(c) : c);
    }
    dst[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */
/*  Label table                                                       */
/* ------------------------------------------------------------------ */

static int label_table_grow(struct irx_label_table *lt)
{
    int new_cap = lt->cap == 0 ? 8 : lt->cap * 2;
    struct irx_label *new_entries;
    size_t new_size = (size_t)new_cap * sizeof(struct irx_label);

    new_entries = (struct irx_label *)
        lt->alloc->alloc(new_size, lt->alloc->ctx);
    if (new_entries == NULL) return -1;

    if (lt->count > 0 && lt->entries != NULL) {
        memcpy(new_entries, lt->entries,
               (size_t)lt->count * sizeof(struct irx_label));
    }
    if (lt->entries != NULL && lt->cap > 0) {
        lt->alloc->dealloc(lt->entries,
                           (size_t)lt->cap * sizeof(struct irx_label),
                           lt->alloc->ctx);
    }
    lt->entries = new_entries;
    lt->cap     = new_cap;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Exec stack                                                        */
/* ------------------------------------------------------------------ */

static int exec_stack_grow(struct irx_exec_stack *es)
{
    int new_cap = es->cap == 0 ? EXEC_STACK_INIT_CAP : es->cap * 2;
    struct irx_exec_frame *new_frames;
    size_t new_size = (size_t)new_cap * sizeof(struct irx_exec_frame);

    new_frames = (struct irx_exec_frame *)
        es->alloc->alloc(new_size, es->alloc->ctx);
    if (new_frames == NULL) return -1;

    if (es->top > 0 && es->frames != NULL) {
        memcpy(new_frames, es->frames,
               (size_t)es->top * sizeof(struct irx_exec_frame));
    }
    if (es->frames != NULL && es->cap > 0) {
        es->alloc->dealloc(es->frames,
                           (size_t)es->cap * sizeof(struct irx_exec_frame),
                           es->alloc->ctx);
    }
    es->frames = new_frames;
    es->cap    = new_cap;
    return 0;
}

/* ================================================================== */
/*  Public API                                                        */
/* ================================================================== */

int irx_ctrl_init(struct irx_parser *p)
{
    struct irx_label_table *lt;
    struct irx_exec_stack  *es;
    size_t lt_size = sizeof(struct irx_label_table);
    size_t es_size = sizeof(struct irx_exec_stack);

    lt = (struct irx_label_table *)
        p->alloc->alloc(lt_size, p->alloc->ctx);
    if (lt == NULL) return IRXPARS_NOMEM;
    memset(lt, 0, lt_size);
    lt->alloc = p->alloc;

    es = (struct irx_exec_stack *)
        p->alloc->alloc(es_size, p->alloc->ctx);
    if (es == NULL) {
        p->alloc->dealloc(lt, lt_size, p->alloc->ctx);
        return IRXPARS_NOMEM;
    }
    memset(es, 0, es_size);
    es->alloc = p->alloc;

    p->label_table = lt;
    p->exec_stack  = es;
    return IRXPARS_OK;
}

void irx_ctrl_cleanup(struct irx_parser *p)
{
    struct irx_label_table *lt;
    struct irx_exec_stack  *es;

    if (p == NULL) return;

    lt = (struct irx_label_table *)p->label_table;
    if (lt != NULL) {
        if (lt->entries != NULL && lt->cap > 0) {
            lt->alloc->dealloc(lt->entries,
                               (size_t)lt->cap * sizeof(struct irx_label),
                               lt->alloc->ctx);
        }
        p->alloc->dealloc(lt, sizeof(struct irx_label_table),
                          p->alloc->ctx);
        p->label_table = NULL;
    }

    es = (struct irx_exec_stack *)p->exec_stack;
    if (es != NULL) {
        if (es->frames != NULL && es->cap > 0) {
            es->alloc->dealloc(es->frames,
                               (size_t)es->cap * sizeof(struct irx_exec_frame),
                               es->alloc->ctx);
        }
        p->alloc->dealloc(es, sizeof(struct irx_exec_stack),
                          p->alloc->ctx);
        p->exec_stack = NULL;
    }
}

int irx_ctrl_label_scan(struct irx_parser *p)
{
    struct irx_label_table *lt =
        (struct irx_label_table *)p->label_table;
    int i;

    if (lt == NULL) return IRXPARS_NOMEM;

    /* Reset existing table from any prior scan. */
    lt->count = 0;

    for (i = 0; i + 1 < p->tok_count; i++) {
        const struct irx_token *t0 = &p->tokens[i];
        const struct irx_token *t1 = &p->tokens[i + 1];

        if (t0->tok_type != TOK_SYMBOL) continue;
        if (t0->tok_flags & TOKF_CONSTANT) continue;
        if (t1->tok_type != TOK_SEMICOLON) continue;
        if (t1->tok_length != 1 || t1->tok_text[0] != ':') continue;

        /* Found SYMBOL ':' — a label. */
        if (lt->count >= lt->cap) {
            if (label_table_grow(lt) != 0) return IRXPARS_NOMEM;
        }
        struct irx_label *lbl = &lt->entries[lt->count];
        lbl->name_len = tok_to_upper_name(t0, lbl->name,
                                           CTRL_NAME_MAX);
        lbl->tok_pos  = i;   /* position of the SYMBOL token     */
        lt->count++;
    }
    return IRXPARS_OK;
}

int irx_ctrl_label_find(struct irx_parser *p,
                        const char *name, int name_len)
{
    struct irx_label_table *lt =
        (struct irx_label_table *)p->label_table;
    int i;

    if (lt == NULL || name == NULL || name_len <= 0) return -1;

    for (i = 0; i < lt->count; i++) {
        if (lt->entries[i].name_len == name_len &&
            memcmp(lt->entries[i].name, name, (size_t)name_len) == 0) {
            return lt->entries[i].tok_pos;
        }
    }
    return -1;
}

struct irx_exec_frame *irx_ctrl_frame_push(struct irx_parser *p,
                                           int frame_type)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    struct irx_exec_frame *frame;

    if (es == NULL) return NULL;
    if (es->top >= es->cap) {
        if (exec_stack_grow(es) != 0) return NULL;
    }
    frame = &es->frames[es->top++];
    memset(frame, 0, sizeof(*frame));
    frame->frame_type = frame_type;
    frame->loop_end   = -1;
    frame->select_end = -1;
    frame->first_iter = 1;
    return frame;
}

struct irx_exec_frame *irx_ctrl_frame_top(struct irx_parser *p)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    if (es == NULL || es->top <= 0) return NULL;
    return &es->frames[es->top - 1];
}

void irx_ctrl_frame_pop(struct irx_parser *p)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    if (es == NULL || es->top <= 0) return;
    es->top--;
}

int irx_ctrl_find_do(struct irx_parser *p,
                     const char *label, int label_len)
{
    struct irx_exec_stack *es = (struct irx_exec_stack *)p->exec_stack;
    int i;

    if (es == NULL) return -1;

    for (i = es->top - 1; i >= 0; i--) {
        if (es->frames[i].frame_type != FRAME_DO) continue;
        if (label == NULL || label_len <= 0) return i;  /* any DO */
        if (es->frames[i].do_label_len == label_len &&
            memcmp(es->frames[i].do_label, label,
                   (size_t)label_len) == 0) {
            return i;
        }
    }
    return -1;
}
