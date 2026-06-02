/* WP-BC-RT01 minimal RC24 repro.
 * Token-walk (REXX370_BYTECODE=0): avar.1.2=[1.01] / result=[1.111], RC=0.
 * Bytecode (default): avar.1.2=[ 1.0  1 ], then RC24 (IRXBC_ERR_ARITH).
 * Cause: abuttal 1.0''loop mis-compiled to blank-concat (bcom.c:2238). */
loop=1
avar.=1.0''loop
say 'avar.1.2=['avar.1.2']'
avar.1.2=avar.1.2*1.1
say 'result=['avar.1.2']'
