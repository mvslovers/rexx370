/* WP-BC-RT01 clean root-cause demo (no crash).
 * Abuttal concatenation: 'a'v'b' has no blanks and no || operator.
 * Token-walk (correct): aXb / [X].
 * Bytecode (wrong):      a X b / [ X ]  (blank inserted at each junction).
 * Cause: bc_exp3 always emits OP_BCONCAT (0x61) for implicit concat
 *        instead of OP_CONCAT (0x60) for abuttal (bcom.c:2238). */
v='X'
say 'a'v'b'
say '['v']'
