/* ------------------------------------------------------------------ */
/*  irxbops.h - REXX/370 Bytecode Opcode Definitions (WP-BC-03/04)   */
/*                                                                    */
/*  Opcode encoding for the bytecode VM.                             */
/*                                                                    */
/*  Operand-bearing opcodes carry inline operands:                   */
/*    4-byte: op + n:u8 + i16-le offset  (DECFOR)                    */
/*    3-byte: op + i16-le signed offset  (JMP, JF, JT, ITERATE,     */
/*            LEAVE)                                                  */
/*    3-byte: op + u16-le index          (PUSH_LIT, LOAD, STORE,    */
/*            DROP)                                                   */
/*    2-byte: op + u8                    (POP, FORINIT, BYINIT)      */
/*    1-byte: all others                                              */
/*                                                                    */
/*  Jump offsets (i16) are relative to the byte AFTER the full       */
/*  instruction (pc after advancing past operand bytes).             */
/*                                                                    */
/*  (c) 2026 mvslovers - REXX/370 Project                            */
/* ------------------------------------------------------------------ */

#ifndef IRXBOPS_H
#define IRXBOPS_H

/* ================================================================== */
/*  Control opcodes (Phase 1 + Phase 2)                              */
/* ================================================================== */

#define OP_NOP       0x00 /* 1 byte — no operation                   */
#define OP_EXIT      0x01 /* 1 byte — terminate execution, RC=0      */
#define OP_NEWCLAUSE 0x02 /* 1 byte — clause boundary (TRACE hook)   */
#define OP_EXIT_RC   0x03 /* 1 byte — pop TOS, convert to int, exit  */

/* ================================================================== */
/*  Control flow (WP-BC-03)                                           */
/* ================================================================== */

#define OP_JMP 0x04 /* 3 bytes: op + i16 — unconditional jump    */
#define OP_JF  0x05 /* 3 bytes: op + i16 — pop, jump if false   */
#define OP_JT  0x06 /* 3 bytes: op + i16 — pop, jump if true    */

/* ================================================================== */
/*  Stack ops (WP-BC-02)                                             */
/* ================================================================== */

#define OP_PUSH_LIT 0x10 /* 3 bytes: op + idx:u16 — push const[idx] */
#define OP_PUSH_TMP 0x11 /* 1 byte  — reserved for future use       */
#define OP_POP      0x12 /* 2 bytes: op + n:u8 — discard n items    */
#define OP_DUP      0x13 /* 1 byte  — duplicate top of stack        */

/* OP_PUSH_OMITTED (value appended at the high end, like OP_TR_VAR)  */
/* marks an omitted function/CALL argument — the empty slot between  */
/* commas in f(a,,b), f(,b), or f(a,) (WP-BC-ARGOMIT).  It pushes a   */
/* sentinel stack slot (empty string + type_cache OMITTED) consumed   */
/* by the very next OP_CALL / OP_CALL_BIF.  For a BIF the slot is     */
/* delivered as a non-NULL empty Lstr (identical to the token-walk    */
/* interpreter); for an internal routine it sets arg_exists[i]=0 so   */
/* ARG(i,'O')/PARSE ARG see the argument as omitted, NOT a present    */
/* empty string. */
#define OP_PUSH_OMITTED 0x9E /* 1 byte — push omitted-argument marker */

/* ================================================================== */
/*  Variable ops (WP-BC-02)                                          */
/* ================================================================== */

#define OP_LOAD  0x20 /* 3 bytes: op + idx:u16 — push vpool[sym[idx]] */
#define OP_STORE 0x21 /* 3 bytes: op + idx:u16 — pop, store vpool[sym[idx]] */
#define OP_DROP  0x22 /* 3 bytes: op + idx:u16 — drop vpool[sym[idx]] */

/* ================================================================== */
/*  Arithmetic ops (WP-BC-02) — all 1 byte                          */
/*  Binary (pop b, pop a, push result): ADD SUB MUL DIV IDIV MOD POW */
/*  Unary (pop a, push result):         NEG                          */
/* ================================================================== */

#define OP_ADD  0x30 /* a + b                                   */
#define OP_SUB  0x31 /* a - b                                   */
#define OP_MUL  0x32 /* a * b                                   */
#define OP_DIV  0x33 /* a / b  (normal division)                */
#define OP_IDIV 0x34 /* a % b  (integer division, truncate-to-0)*/
#define OP_MOD  0x35 /* a // b (remainder)                      */
#define OP_POW  0x36 /* a ** b                                   */
#define OP_NEG  0x37 /* -a     (unary minus)                    */

/* ================================================================== */
/*  Comparison ops (WP-BC-02) — all 1 byte                          */
/*  Pop b, pop a, push "1" or "0".                                   */
/* ================================================================== */

#define OP_EQ  0x40 /* =   fuzzy equal                         */
#define OP_NE  0x41 /* \=  fuzzy not-equal                     */
#define OP_LT  0x42 /* <   less than                           */
#define OP_LE  0x43 /* <=  less or equal                       */
#define OP_GT  0x44 /* >   greater than                        */
#define OP_GE  0x45 /* >=  greater or equal                    */
#define OP_DEQ 0x46 /* ==  strict equal                        */
#define OP_DNE 0x47 /* \== strict not-equal                    */
#define OP_DLT 0x48 /* <<  strict less                         */
#define OP_DLE 0x49 /* <<= strict less-or-equal                */
#define OP_DGT 0x4A /* >>  strict greater                      */
#define OP_DGE 0x4B /* >>= strict greater-or-equal             */

/* ================================================================== */
/*  Logical/boolean ops (WP-BC-02) — all 1 byte                     */
/*  Boolean values are "0" (false) or "1" (true).                    */
/* ================================================================== */

#define OP_AND 0x50 /* &  — logical and                        */
#define OP_OR  0x51 /* |  — logical or                         */
#define OP_XOR 0x52 /* && — logical exclusive or               */
#define OP_NOT 0x53 /* \  — logical not (unary)                */

/* ================================================================== */
/*  String ops (WP-BC-02) — all 1 byte                              */
/* ================================================================== */

#define OP_CONCAT  0x60 /* concatenation, no blank (|| or abuttal) */
#define OP_BCONCAT 0x61 /* blank concatenation (one blank)         */

/* ================================================================== */
/*  I/O ops (WP-BC-03)                                               */
/* ================================================================== */

#define OP_SAY 0x70 /* 1 byte — pop TOS, write via io_routine     */

/* ================================================================== */
/*  DO loop ops (WP-BC-03)                                           */
/*                                                                    */
/*  FORINIT/BYINIT/DECFOR support the DO count / DO TO / DO BY       */
/*  fast paths.  Counter state lives in a parallel do_frame stack     */
/*  inside the VM (not on the eval stack).                            */
/* ================================================================== */

#define OP_TOINT   0x71 /* 1 byte — coerce TOS to integer string   */
#define OP_FORINIT 0x72 /* 2 bytes: op + n:u8 — pop count → frame[n], push bool (count>0) */
#define OP_BYINIT  0x73 /* 2 bytes: op + n:u8 — reserved           */
#define OP_DECFOR  0x74 /* 4 bytes: op + n:u8 + i16 — dec frame[n], jump-if-done */
#define OP_DOTEST  0x75 /* 1 byte — reserved (WHILE/UNTIL via JF)  */

/* ================================================================== */
/*  Iteration ops (WP-BC-03)                                         */
/*                                                                    */
/*  ITERATE and LEAVE carry compile-time-resolved i16 offsets.        */
/*  They are semantically equivalent to JMP but are kept distinct     */
/*  for disassembly / trace tooling.                                  */
/* ================================================================== */

#define OP_ITERATE 0x76 /* 3 bytes: op + i16 — jump to iterate pt  */
#define OP_LEAVE   0x77 /* 3 bytes: op + i16 — jump to loop end    */

/* ================================================================== */
/*  Function / CALL ops (WP-BC-04)                                    */
/*                                                                    */
/*  OP_LABEL marks a call target in the bytecode stream.  The VM     */
/*  pre-scans for all OP_LABEL instructions at startup to build a    */
/*  label_pc[] table indexed by sym_idx.                             */
/*                                                                    */
/*  OP_CALL: CALL statement — pops nargs from eval stack, tries      */
/*  label_pc[sym_idx] first; falls back to BIF registry.            */
/*  Pushes nothing; result (if any) is available as RESULT variable. */
/*                                                                    */
/*  OP_CALL_BIF: expression function call — pops nargs, tries        */
/*  label_pc[sym_idx] first (push_result=1); falls back to BIF       */
/*  registry.  Pushes return value onto eval stack.                  */
/*                                                                    */
/*  OP_RETURN: return from internal CALL with no value.              */
/*  OP_RETURNV: pop TOS, store as RESULT, return from internal CALL. */
/*  Both opcodes exit RC=0 when there is no active call frame.       */
/* ================================================================== */

#define OP_LABEL    0x78 /* 3 bytes: op + sym_idx:u16 — call target  */
#define OP_CALL     0x79 /* 4 bytes: op + sym_idx:u16 + nargs:u8     */
#define OP_CALL_BIF 0x7A /* 4 bytes: op + sym_idx:u16 + nargs:u8     */
#define OP_RETURN   0x7B /* 1 byte — return, no value                 */
#define OP_RETURNV  0x7C /* 1 byte — pop TOS, store as RESULT, return */

/* ================================================================== */
/*  PROCEDURE EXPOSE opcodes (WP-BC-05 PR B)                         */
/*                                                                    */
/*  OP_PROC is emitted immediately after the function label and       */
/*  before OP_EXPOSE/OP_EXPOSE_INDIRECT instructions.  It creates    */
/*  an isolated variable scope (child vpool) so the callee cannot    */
/*  see the caller's variables except for the ones it EXPOSEs.       */
/*                                                                    */
/*  nexposed is the count of immediately-following OP_EXPOSE and     */
/*  OP_EXPOSE_INDIRECT instructions (informational; the VM processes  */
/*  them individually via normal dispatch).                           */
/*                                                                    */
/*  OP_EXPOSE links the named variable through to the caller's pool. */
/*  OP_EXPOSE_INDIRECT reads the named variable's VALUE from the      */
/*  caller's pool, splits it by whitespace, and exposes each word.   */
/* ================================================================== */

#define OP_PROC            0x8B /* 2 bytes: op + nexposed:u8          */
#define OP_EXPOSE          0x8C /* 3 bytes: op + sym_idx:u16          */
#define OP_EXPOSE_INDIRECT 0x8D /* 3 bytes: op + sym_idx:u16          */

/* ================================================================== */
/*  Compound variable opcodes (WP-BC-05 PR C)                         */
/*                                                                    */
/*  OP_LOAD_STEM pops tail_count values (leftmost first), builds the  */
/*  compound name STEM.tail0.tail1...tailN-1 (tails uppercased), then */
/*  looks up the result in the vpool.  NOVALUE → compound name itself. */
/*                                                                    */
/*  OP_STORE_STEM pops the value (TOS), then pops tail_count tails,  */
/*  builds the compound name, and stores the value in the vpool.      */
/*                                                                    */
/*  OP_DROP_STEM with tail_count=0 drops the entire stem (all entries */
/*  whose name begins with the stem prefix including its trailing dot).*/
/*  With tail_count>0 it pops tails, builds the compound name, and    */
/*  drops only that one entry.                                         */
/*                                                                    */
/*  The stem symbol in the sym-table includes the trailing dot        */
/*  (e.g. "A." for a compound variable A.something).                  */
/* ================================================================== */

#define OP_LOAD_STEM  0x8E /* 4 bytes: op + stem_sym:u16 + tail_count:u8 */
#define OP_STORE_STEM 0x8F /* 4 bytes: op + stem_sym:u16 + tail_count:u8 */
#define OP_DROP_STEM  0x90 /* 4 bytes: op + stem_sym:u16 + tail_count:u8 */

/* ================================================================== */
/*  PARSE sub-VM opcodes (WP-BC-05 PR A + PR C)                      */
/*                                                                    */
/*  PARSE [UPPER] source template [, template ...]                    */
/*                                                                    */
/*  The source string is pushed on the eval stack BEFORE             */
/*  OP_PARSE_BEGIN, which pops it and initialises the parse frame.   */
/*  OP_PUSH_SOURCE / OP_PUSH_NUMERIC are dedicated pushers for the   */
/*  SOURCE and NUMERIC sources whose value is VM-state-dependent.    */
/*  OP_PULL_FROM_QUEUE pushes the next line from the external data   */
/*  queue (WP-33b stub — returns IRXBC_ERR_UNSUP at runtime).       */
/*                                                                    */
/*  Within a template, items are each followed immediately by a      */
/*  trigger opcode that determines how far to scan:                   */
/*    OP_PVAR         — simple variable target (sym_idx:u16)          */
/*    OP_PVAR_STEM    — compound variable target (stem_sym:u16 +      */
/*                      tail_count:u8, tails already pushed on stack) */
/*    OP_PDOT         — dot placeholder (consumes but discards)       */
/*  Triggers:                                                         */
/*    OP_TR_SPACE — one word (leading whitespace stripped)            */
/*    OP_TR_LIT   — up to the first occurrence of a literal string   */
/*    OP_TR_ABS   — up to an absolute column (1-based; inline u16)   */
/*    OP_TR_REL   — by a signed relative offset (inline i16)         */
/*    OP_TR_END   — rest of the source string (last item in segment)  */
/*    OP_TR_VAR   — like TR_LIT but delimiter fetched from variable   */
/*                  at runtime (indirect pattern `(var)`)             */
/*                                                                    */
/*  When no template items precede a position trigger, the compiler   */
/*  emits OP_PDOT + trigger to silently advance the scan position.   */
/* ================================================================== */

#define OP_PARSE_BEGIN  0x80 /* 2 bytes: op + flags:u8 (bit0=UPPER)  */
#define OP_PARSE_END    0x81 /* 1 byte — end parse frame, free source */
#define OP_PVAR         0x82 /* 3 bytes: op + sym_idx:u16             */
#define OP_PDOT         0x83 /* 1 byte — dot placeholder              */
#define OP_TR_SPACE     0x84 /* 1 byte — trigger: one word            */
#define OP_TR_LIT       0x85 /* 3 bytes: op + lit_idx:u16             */
#define OP_TR_ABS       0x86 /* 3 bytes: op + col:u16  (1-based)      */
#define OP_TR_REL       0x87 /* 3 bytes: op + off:i16  (signed)       */
#define OP_TR_END       0x88 /* 1 byte — trigger: rest of string      */
#define OP_PUSH_SOURCE  0x89 /* 1 byte — push PARSE SOURCE string     */
#define OP_PUSH_NUMERIC 0x8A /* 1 byte — push PARSE NUMERIC string    */
#define OP_TR_VAR       0x9D /* 3 bytes: op + sym_idx:u16 — indirect  */
                             /* pattern: value of variable used as    */
                             /* literal delimiter at runtime          */

/* ================================================================== */
/*  PARSE compound-target opcode (WP-BC-05 PR C)                     */
/*                                                                    */
/*  OP_PVAR_STEM is emitted for compound variable targets in PARSE    */
/*  templates (e.g. PARSE ARG a.1, PARSE VAR x a.i b.j).            */
/*  Before OP_PVAR_STEM, the compiler pushes tail_count values onto  */
/*  the eval stack (constant tails via OP_PUSH_LIT, variable tails   */
/*  via OP_LOAD).  The VM pops them, builds the compound name, and   */
/*  stores it in the parse frame; the following trigger opcode then   */
/*  assigns the matched substring to that compound variable.          */
/* ================================================================== */

#define OP_PVAR_STEM       0x91 /* 4 bytes: op + stem_sym:u16 + tail_count:u8 */
#define OP_PULL_FROM_QUEUE 0x92 /* 1 byte — push next queue line (WP-33b stub) */

/* ================================================================== */
/*  SIGNAL opcodes (WP-BC-07)                                          */
/*                                                                    */
/*  OP_SIGNAL:       unconditional jump to a compile-time-known label. */
/*  OP_SIGNAL_VALUE: pop label-name string, resolve at runtime, jump.  */
/*  OP_SIGNAL_ON:    enable condition trap with given handler label.    */
/*  OP_SIGNAL_OFF:   disable condition trap.                           */
/*                                                                    */
/*  Both SIGNAL opcodes clear the eval stack, unwind all call frames,  */
/*  and set SIGL.  They are equivalent to a non-local goto.            */
/*                                                                    */
/*  OP_SIGNAL_ON / OP_SIGNAL_OFF: compiled but VM handling deferred   */
/*  to WP-BC-07 PR B (condition-trap mechanics).                      */
/* ================================================================== */

#define OP_SIGNAL       0x93 /* 3 bytes: op + sym_idx:u16               */
#define OP_SIGNAL_VALUE 0x94 /* 1 byte  — pop label name, jump          */
#define OP_SIGNAL_ON    0x95 /* 4 bytes: op + cond:u8 + sym_idx:u16     */
#define OP_SIGNAL_OFF   0x96 /* 2 bytes: op + cond:u8                   */

/* ================================================================== */
/*  TRACE + ADDRESS opcodes (WP-BC-08)                                */
/*                                                                    */
/*  TRACE:                                                            */
/*    OP_TRACE_TOGGLE — bare TRACE: toggle wkbi_interactive, keep     */
/*                      wkbi_trace letter.                            */
/*    OP_TRACE_SET    — constant option: mode byte = letter | 0x80    */
/*                      if interactive.  Valid letters: NAILRCFEO.    */
/*    OP_TRACE_VALUE  — dynamic option: pop string, parse first char, */
/*                      set wkbi_trace + wkbi_interactive.            */
/*                                                                    */
/*  ADDRESS:                                                          */
/*    OP_ADDRESS_TOGGLE — bare ADDRESS: swap wkbi_address and         */
/*                        wkbi_prev_address (SC28-1883-0 §6.1).       */
/*    OP_ADDRESS_SET    — constant env: save prev, set from sym table. */
/*    OP_ADDRESS_VALUE  — dynamic env: pop string, save prev, set.    */
/* ================================================================== */

#define OP_TRACE_TOGGLE   0x97 /* 1 byte — toggle wkbi_interactive          */
#define OP_TRACE_SET      0x98 /* 2 bytes: op + mode:u8                     */
#define OP_TRACE_VALUE    0x99 /* 1 byte — pop string, parse, set fields    */
#define OP_ADDRESS_TOGGLE 0x9A /* 1 byte — swap address + prev_address      */
#define OP_ADDRESS_SET    0x9B /* 3 bytes: op + sym_idx:u16                 */
#define OP_ADDRESS_VALUE  0x9C /* 1 byte — pop string, save prev, set addr  */

/* ================================================================== */
/*  Per-opcode size in bytes (including the opcode byte itself)       */
/* ================================================================== */

/* clang-format off */
#define OP_SIZE(op)                        \
    (((op) == OP_PUSH_LIT)  ? 3 :         \
     ((op) == OP_POP)       ? 2 :         \
     ((op) == OP_LOAD)      ? 3 :         \
     ((op) == OP_STORE)     ? 3 :         \
     ((op) == OP_DROP)      ? 3 :         \
     ((op) == OP_JMP)       ? 3 :         \
     ((op) == OP_JF)        ? 3 :         \
     ((op) == OP_JT)        ? 3 :         \
     ((op) == OP_FORINIT)   ? 2 :         \
     ((op) == OP_BYINIT)    ? 2 :         \
     ((op) == OP_DECFOR)    ? 4 :         \
     ((op) == OP_ITERATE)   ? 3 :         \
     ((op) == OP_LEAVE)     ? 3 :         \
     ((op) == OP_LABEL)       ? 3 :         \
     ((op) == OP_CALL)        ? 4 :         \
     ((op) == OP_CALL_BIF)    ? 4 :         \
     ((op) == OP_PARSE_BEGIN) ? 2 :         \
     ((op) == OP_PVAR)        ? 3 :         \
     ((op) == OP_TR_LIT)      ? 3 :         \
     ((op) == OP_TR_ABS)      ? 3 :         \
     ((op) == OP_TR_REL)      ? 3 :         \
     ((op) == OP_TR_VAR)      ? 3 :         \
     ((op) == OP_PROC)            ? 2 :         \
     ((op) == OP_EXPOSE)          ? 3 :         \
     ((op) == OP_EXPOSE_INDIRECT) ? 3 :         \
     ((op) == OP_LOAD_STEM)       ? 4 :         \
     ((op) == OP_STORE_STEM)      ? 4 :         \
     ((op) == OP_DROP_STEM)       ? 4 :         \
     ((op) == OP_PVAR_STEM)       ? 4 :         \
     ((op) == OP_SIGNAL)       ? 3 :         \
     ((op) == OP_SIGNAL_ON)    ? 4 :         \
     ((op) == OP_SIGNAL_OFF)   ? 2 :         \
     ((op) == OP_TRACE_SET)    ? 2 :         \
     ((op) == OP_ADDRESS_SET)  ? 3 :         \
     1)
/* clang-format on */

/* ================================================================== */
/*  Compiler and VM return codes                                      */
/* ================================================================== */

#define IRXBC_OK                 0  /* success                               */
#define IRXBC_ERR_STOR           20 /* irxstor allocation failed             */
#define IRXBC_ERR_TOKN           21 /* tokenizer returned an error           */
#define IRXBC_ERR_UNSUP          22 /* unsupported construct                 */
#define IRXBC_ERR_OPCODE         23 /* unknown opcode encountered by VM      */
#define IRXBC_ERR_ARITH          24 /* arithmetic error (type, divzero etc.) */
#define IRXBC_ERR_BOOL           32 /* logical value not 0 or 1 (OP_AND/OR/XOR/NOT/JF/JT) */
#define IRXBC_ERR_STACK          25 /* stack underflow or overflow           */
#define IRXBC_ERR_PATCH          26 /* too many forward-jump patches         */
#define IRXBC_ERR_LOOP           27 /* DO nesting too deep                   */
#define IRXBC_ERR_IO             28 /* I/O routine call failed               */
#define IRXBC_ERR_STRTOOLONG     29 /* literal/symbol exceeds IRXBC_STR_MAX  */
#define IRXBC_ERR_CALL           30 /* CALL stack overflow (IRXBC_CALL_DEPTH)    */
#define IRXBC_ERR_PARSE_COMPOUND 31 /* compound-variable target in PARSE template */

#endif /* IRXBOPS_H */
