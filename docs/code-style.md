## Code style

This project uses **C99 / gnu99** (see `project.toml`: `cflags = ["-std=gnu99"]`).
We do **NOT** target C89. Do not write C89-compatible code.

Formatting is enforced by `.clang-format`. Static analysis by `.clang-tidy`.
Run before every commit:

```bash
clang-format -i src/*.c include/*.h test/*.c
clang-tidy src/*.c -- -I./include -std=gnu99
```

### C99 rules we use

- Declare variables where they are first used, not at the top of the block
- `for (int i = 0; ...)` is fine
- `//` single-line comments are fine
- Do NOT create bare `{}` blocks just to introduce a declaration scope.
  If a block exists only because "C89 requires declarations at top" — remove it.
  The only legitimate bare `{}` is when you genuinely need a narrower scope
  for resource cleanup.

### Indentation

4 spaces. No tabs. Tabs in source files are a bug.

### Braces: Allman / BSD style

Opening brace on its own line. Always. Even for single-line bodies.

```c
int irxstor(int function, int length, void **addr_ptr, struct envblock *envblock)
{
    if (length <= 0)
    {
        return 20;
    }
    return 0;
}
```

### Braces on single-line bodies: required

Always use braces, even for one-line if/for/while/do bodies:

```c
/* WRONG */
if (rc != 0) return 20;
for (i = 0; i < n; i++) count++;

/* RIGHT */
if (rc != 0)
{
    return 20;
}
for (i = 0; i < n; i++)
{
    count++;
}
```

### Switch/case

Case labels indented one level inside switch. Braces around case bodies.

**Note:** `clang-format` cannot auto-insert braces around case bodies
(known limitation). It will preserve them if present. This is a manual
convention — always add `{}` around case bodies when writing new code.

```c
switch (function)
{
    case RXSMGET:
    {
        void *ptr = calloc(1, (size_t)length);
        if (ptr == NULL)
        {
            return 20;
        }
        *addr_ptr = ptr;
        return 0;
    }
    case RXSMFRE:
    {
        free(*addr_ptr);
        *addr_ptr = NULL;
        return 0;
    }
    default:
    {
        return 20;
    }
}
```

### Naming conventions

| Element | Style | Example |
|---|---|---|
| Variables | snake_case | `max_value`, `tok_count`, `bucket_count` |
| Functions | snake_case | `vpool_create`, `irx_tokn_run` |
| Types (typedef) | snake_case with `_t` suffix | `user_t`, `lstr_alloc_t` |
| Structs | snake_case (no suffix) | `struct irx_vpool`, `struct irx_token` |
| Enums | snake_case | `enum tok_type` |
| Enum values | ALL_CAPS | `TOK_SYMBOL`, `VPOOL_OK` |
| Macros / constants | ALL_CAPS | `MAX_BUFFER_SIZE`, `ENVBLOCK_ID` |
| Library prefix | yes, always | `irx_`, `vpool_`, `lstr_`, `Lstr` |

Short/terse names are OK for system-level code:

```c
int i, j, k;       /* loop counters */
char *p;            /* pointer */
int rc;             /* return code */
int len, n;         /* lengths, counts */
char *buf, *tmp;    /* buffers */
```

### Pointer style

`*` attached to the name, not the type:

```c
char *ptr;
int *values;
struct envblock *envblock;
void **addr_ptr;
```

### Types: prefer typedef over raw struct

```c
/* Definition */
typedef struct irx_token_s
{
    unsigned char tok_type;
    unsigned char tok_flags;
    short         tok_col;
    int           tok_line;
} irx_token_t;

/* Usage: prefer the typedef */
irx_token_t *tokens;          /* good */
struct irx_token_s *tokens;   /* acceptable but verbose */
```

Exception: the IBM-compatible control blocks in `irx.h` keep their existing
`struct` names for compatibility. Don't rename those.

### Enums over #define chains

Prefer C99 enums for related constants:

```c
/* Prefer this */
enum tok_type
{
    TOK_SYMBOL     = 0x01,
    TOK_STRING     = 0x02,
    TOK_NUMBER     = 0x03,
};

/* Over this */
#define TOK_SYMBOL   0x01
#define TOK_STRING   0x02
#define TOK_NUMBER   0x03
```

Exception: values that must be usable in `#if` preprocessor conditions
stay as `#define`.

### No magic numbers

```c
/* WRONG */
if (bucket_count > 4 * entry_count) resize();
buf = malloc(1024);

/* RIGHT */
#define VP_MAX_LOAD  4
#define IO_BUF_SIZE  1024

if (bucket_count > VP_MAX_LOAD * entry_count) resize();
buf = malloc(IO_BUF_SIZE);
```

### Header guards

Use `#ifndef` / `#define` / `#endif`, not `#pragma once`:

```c
#ifndef IRXTOKN_H
#define IRXTOKN_H

/* ... */

#endif /* IRXTOKN_H */
```

Guard name: header filename in ALL_CAPS, dots replaced by underscores.
No leading double underscores (reserved by C standard).

### Include order

System headers first (angle brackets), then project headers (quotes).
Blank line between groups:

```c
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "irx.h"
#include "irxwkblk.h"
#include "irxfunc.h"
```

### Return type on same line as function name

```c
int irx_tokn_run(struct envblock *envblock, const char *src, int src_len)
{
    ...
}
```

### Line length

No hard limit. Let the code breathe. Don't wrap lines just to hit 80 columns.
If a function signature or expression is naturally long, let it be long.
Clang-format is configured with `ColumnLimit: 0`.
