\ lib/scheme-jit.fs - JIT a small Scheme subset to native code via the IR.
\ Supports one fixnum parameter `x`, fixnum literals and (+ - *) with
\ nesting.  The compiled-function record lives in the object store
\ (lib/objects.fs), exercising its allocator and pointer tracing.

REQUIRE scheme.fs
REQUIRE objects.fs

SYSTEM NEWTYPE JITF
  0 FIELD arity
  1 FIELD code
;TYPE

\ ---- IR text buffer -------------------------------------------------------
CREATE IRB 8192 ALLOT
VARIABLE IRP
: IR-RESET  IRB IRP ! ;
: IR-APP { a u -- } a IRP @ u MOVE  IRP @ u + IRP ! ;
: TOK { a u -- } a u IR-APP  32 IRP @ C!  1 IRP +! ;

CREATE NBUF 24 ALLOT
: >DEC { n -- a u }
  NBUF 24 + TO a
  a TO u
  BEGIN u 1- TO u  n 10 MOD 48 + u C!  n 10 / TO n  n 0= UNTIL
  u  a u - ;
: IMM ( n -- ) S" #" IR-APP  >DEC IR-APP  32 IRP @ C!  1 IRP +! ;

S" +" SYM-INTERN CONSTANT S-+
S" -" SYM-INTERN CONSTANT S--
S" *" SYM-INTERN CONSTANT S-*

\ ---- emit native code for an expression; result pushed on the data stack --
: EMIT-EXPR { e -- }
  e PAIR? IF
    e CAR S-+ = IF
      e CDR CAR RECURSE  e CDR CDR CAR RECURSE
      S" spop R0" TOK  S" spop R1" TOK  S" add R0, R1, R0" TOK  S" spush R0" TOK EXIT
    THEN
    e CAR S-- = IF
      e CDR CAR RECURSE  e CDR CDR CAR RECURSE
      S" spop R0" TOK  S" spop R1" TOK  S" sub R0, R1, R0" TOK  S" spush R0" TOK EXIT
    THEN
    e CAR S-* = IF
      e CDR CAR RECURSE  e CDR CDR CAR RECURSE
      S" spop R0" TOK  S" spop R1" TOK  S" mul R0, R1, R0" TOK
      S" lshr R0, R0, #3" TOK  S" spush R0" TOK EXIT
    THEN
    -1 THROW
  THEN
  e SYM? IF  S" mov R0, [SP]" TOK  S" spush R0" TOK EXIT  THEN
  S" spush" TOK   e IMM     \ fixnum literal (tagged; add is tag-preserving)
;

\ ---- compile a (lambda (x) body): native word ( x -- body ) --------------
\ The sljit block is copied into the store's RWX code arena and released, so
\ the JITF record holds a code object (kept alive by GC) rather than raw code.
: JIT1 { e store -- codeobj jitf raw }
  IR-RESET
  S" locals 8" TOK
  S" mov R0, [S1-1c]" TOK  S" mov [SP], R0" TOK  S" sdrop" TOK
  e EMIT-EXPR
  IRB  IRP @ IRB -  ASSEMBLE-FORTH-DYN TO raw
  raw JIT-SIZE store NEW-CODE TO codeobj
  raw (SLJIT-FREE)
  JITF store NEW TO jitf
  codeobj jitf 1 FIELD!
  1       jitf 0 FIELD!
  jitf
;

\ ---- read + run -----------------------------------------------------------
: PARSE1 { c-addr u -- } c-addr S-IN ! u S-LEN ! 0 S-POS ! READ ;
: JIT-RUN1 ( x expr store -- y ) JIT1 1 FIELD@ CODE-PTR CALLF1 ;
