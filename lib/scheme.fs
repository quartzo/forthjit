\ lib/scheme.fs - a minimal Scheme, loaded with `REQUIRE scheme.fs`.
\ Values are tagged cells (low 3 bits):
\   0 fixnum   1 pair   2 symbol   3 string   4 closure   5 primitive
\   7 specials: NIL(7) SFALSE(15) STRUE(23) SVOID(31)
\ Pairs and closures are GC objects in the object store (lib/objects.fs):
\ their pointer fields hold tagged values, so the store's mark mask is set to
\ 7 to strip tags while tracing.  Symbols and strings are interned/immutable
\ and allocated from the store's meta arena.

REQUIRE objects.fs
REQUIRE hashtable.fs

\ --------------------------------------------------------------------------
0 CONSTANT T-FIX
1 CONSTANT T-PAIR
2 CONSTANT T-SYM
3 CONSTANT T-STR
4 CONSTANT T-CLO
5 CONSTANT T-PRIM
6 CONSTANT T-VEC
7 CONSTANT NIL
15 CONSTANT SFALSE
23 CONSTANT STRUE
31 CONSTANT SVOID

: >FIX ( n -- x ) 8 * ;
: FIX> ( x -- n ) 8 / ;
: TAG ( x -- t ) 7 AND ;
: PTR ( x -- a ) -8 AND ;
: FIX? TAG T-FIX = ;
: PAIR? TAG T-PAIR = ;
: SYM? TAG T-SYM = ;
: STR? TAG T-STR = ;
: CLO? TAG T-CLO = ;
: PRIM? TAG T-PRIM = ;
: VEC? TAG T-VEC = ;
: TRUTHY? SFALSE <> ;

\ ---- object-store glue ---------------------------------------------------
\ Scheme uses the current context's store (SYSTEM in the root).  The mark
\ mask strips the low tag bits so GC can recognize tagged pair/closure
\ pointers; symbols/strings are interned in the meta arena.
: SCM-STORE ( -- store ) TASK-STORE ;
7 SCM-STORE STORE-MASK!

SYSTEM NEWTYPE SCM-PAIR
  1 FIELD car
  1 FIELD cdr
;TYPE

SYSTEM NEWTYPE SCM-CLO
  1 FIELD scm-params
  1 FIELD scm-body
  1 FIELD scm-env
;TYPE

: CONS { a d -- p }
  SCM-PAIR SCM-STORE NEW TO p
  a p 0 FIELD!
  d p 1 FIELD!
  p T-PAIR OR ;
: CAR ( p -- a ) PTR 0 FIELD@ ;
: CDR ( p -- d ) PTR 1 FIELD@ ;
: SET-CAR ( x p -- ) PTR 0 FIELD! ;
: SET-CDR ( x p -- ) PTR 1 FIELD! ;

\ ---- vectors (a tagged store ARRAY) --------------------------------------
: VMAKE { n -- v } n SCM-STORE ARRAY-NEW T-VEC OR ;
: VLEN  { v -- n } v PTR ARRAY-LEN ;
: VREF  { v i -- x } v PTR i ARRAY@ ;
: VSET  { x v i -- } x v PTR i ARRAY! ;

DEFER EVAL

\ --------------------------------------------------------------------------
\ symbols (the intern list lives in the store; SYMS returns its field address)
: SYMS ( -- addr ) SCM-STORE S-SYMBOLS CELLS + ;
: SYM-LEN PTR @ ;
: SYM-DATA PTR 8 + ;
: STREQ? { a1 a2 n -- f }
  n 0 ?DO a1 I + C@ a2 I + C@ <> IF 0 UNLOOP EXIT THEN LOOP -1 ;
: SYM-MAKE { c-addr u -- s }
  u 8 + SCM-STORE META-ALLOC TO s
  u s !
  c-addr s 8 + u MOVE
  s T-SYM OR ;
: SYM-FIND { c-addr u -- s }
  SYMS @ TO s
  BEGIN s PAIR? WHILE
    s CAR SYM-LEN u = IF s CAR SYM-DATA c-addr u STREQ? IF s CAR EXIT THEN THEN
    s CDR TO s
  REPEAT 0 ;
: SYM-INTERN { c-addr u -- s }
  c-addr u SYM-FIND ?DUP IF EXIT THEN
  c-addr u SYM-MAKE
  DUP SYMS @ CONS SYMS ! ;

\ --------------------------------------------------------------------------
\ special-form symbols (needed by the reader and the evaluator)
S" quote"  SYM-INTERN CONSTANT SYM-QUOTE
S" if"     SYM-INTERN CONSTANT SYM-IF
S" lambda" SYM-INTERN CONSTANT SYM-LAMBDA
S" define" SYM-INTERN CONSTANT SYM-DEFINE
S" set!"   SYM-INTERN CONSTANT SYM-SET!
S" begin"  SYM-INTERN CONSTANT SYM-BEGIN
S" let"    SYM-INTERN CONSTANT SYM-LET
S" and"    SYM-INTERN CONSTANT SYM-AND
S" or"     SYM-INTERN CONSTANT SYM-OR

\ --------------------------------------------------------------------------
\ reader
VARIABLE S-IN
VARIABLE S-LEN
VARIABLE S-POS
: S-END? S-POS @ S-LEN @ >= ;
: S-PEEK S-IN @ S-POS @ + C@ ;
: S-SKIP
  BEGIN S-END? 0= WHILE S-PEEK 32 <= IF 1 S-POS +! ELSE EXIT THEN REPEAT ;
: DELIM? { c -- f } c 32 <= IF -1 EXIT THEN c 40 = IF -1 EXIT THEN c 41 = IF -1 EXIT THEN 0 ;
: S-TOKEN { -- a u start }
  S-POS @ TO start
  BEGIN S-END? 0= IF S-PEEK DELIM? 0= ELSE 0 THEN WHILE 1 S-POS +! REPEAT
  S-IN @ start + TO a
  S-POS @ start - TO u
  a u ;
: TO-NUM? { a u -- i n neg }
  u 0= IF 0 0 EXIT THEN
  a C@ 45 = IF 1 ELSE 0 THEN TO neg
  neg u >= IF 0 0 EXIT THEN
  a neg + TO i
  0 TO n
  u neg - 0 ?DO
    i I + C@ DUP 48 < OVER 57 > OR IF DROP 0 0 UNLOOP EXIT THEN
    48 - n 10 * + TO n
  LOOP
  n neg IF NEGATE THEN -1 ;
: READ-STRING { -- a u start s }
  S-POS @ 1+ TO start
  BEGIN S-END? 0= WHILE S-PEEK 34 = IF EXIT THEN 1 S-POS +! REPEAT
  S-IN @ start + TO a
  S-POS @ start - TO u
  1 S-POS +!
  u 8 + SCM-STORE META-ALLOC TO s
  u s !
  a s 8 + u MOVE
  s T-STR OR ;
DEFER READ
: READ-LIST { -- o }
  S-SKIP
  S-END? IF NIL EXIT THEN
  S-PEEK 41 = IF 1 S-POS +! NIL EXIT THEN
  READ
  RECURSE
  CONS ;
: (READ) { -- c o }
  S-SKIP
  S-END? IF NIL EXIT THEN
  S-PEEK TO c
  c 40 = IF 1 S-POS +! READ-LIST EXIT THEN
  c 41 = IF 1 S-POS +! NIL EXIT THEN
  c 34 = IF 1 S-POS +! READ-STRING EXIT THEN
  c 39 = IF 1 S-POS +! READ NIL CONS SYM-QUOTE SWAP CONS EXIT THEN
  S-TOKEN
  2DUP S" #t" COMPARE 0= IF 2DROP STRUE EXIT THEN
  2DUP S" #f" COMPARE 0= IF 2DROP SFALSE EXIT THEN
  2DUP TO-NUM? IF NIP NIP >FIX EXIT THEN
  DROP
  SYM-INTERN ;
' (READ) IS READ

\ --------------------------------------------------------------------------
\ environments
\ A frame is an ARRAY of 2N cells [key0 val0 key1 val1 ...] (raw, untagged).
: LIST-LEN { l -- } 0 BEGIN l PAIR? WHILE 1+ l CDR TO l REPEAT ;
: FRAME-LOOKUP { sym frame -- val }
  frame 0= IF 0 EXIT THEN
  frame ARRAY-LEN 2 / 0 ?DO
    frame 2 I * ARRAY@ sym = IF frame 2 I * 1+ ARRAY@ UNLOOP EXIT THEN
  LOOP 0 ;
: ENV-GLOBAL ( -- ht ) SCM-STORE STORE-BINDINGS ;

: ENV-LOOKUP { sym env -- val }
  env
  BEGIN DUP PAIR? WHILE
    sym OVER CAR FRAME-LOOKUP
    DUP IF NIP EXIT THEN DROP
    CDR
  REPEAT DROP
  sym ENV-GLOBAL HT-GET ;
: ENV-DEFINE { sym val env -- old nf i }
  env PAIR? IF
    env CAR TO old
    old 0= IF sym val 2 SCM-STORE ARRAY-NEW env SET-CAR EXIT THEN
    old ARRAY-LEN 2 + SCM-STORE ARRAY-NEW TO nf
    old ARRAY-LEN 0 ?DO old I ARRAY@ nf I ARRAY! LOOP
    sym nf old ARRAY-LEN ARRAY!
    val nf old ARRAY-LEN 1+ ARRAY!
    nf env SET-CAR
  ELSE
    val sym ENV-GLOBAL HT-PUT
  THEN ;
: ENV-SET { sym val env -- c frame i }
  env TO c
  BEGIN c PAIR? WHILE
    c CAR TO frame
    frame 0= 0= IF
      frame ARRAY-LEN 2 / 0 ?DO
        frame 2 I * ARRAY@ sym = IF
          val frame 2 I * 1+ ARRAY!  -1 UNLOOP EXIT
        THEN
      LOOP
    THEN
    c CDR TO c
  REPEAT
  val sym ENV-GLOBAL HT-PUT  0 ;
: ENV-EXTEND { syms vals env -- f i }
  syms LIST-LEN 2 * SCM-STORE ARRAY-NEW TO f
  0 TO i
  BEGIN syms PAIR? WHILE
    syms CAR f i ARRAY!
    vals CAR f i 1+ ARRAY!
    i 2 + TO i
    syms CDR TO syms
    vals CDR TO vals
  REPEAT
  f env CONS ;

\ --------------------------------------------------------------------------
\ printing
: WRITE { x -- }
  x NIL = IF 40 EMIT 41 EMIT EXIT THEN
  x SFALSE = IF 35 EMIT 102 EMIT EXIT THEN
  x STRUE = IF 35 EMIT 116 EMIT EXIT THEN
  x SVOID = IF EXIT THEN
  x FIX? IF x FIX> . EXIT THEN
  x SYM? IF x SYM-DATA x SYM-LEN TYPE EXIT THEN
  x STR? IF 34 EMIT x PTR 8 + @ x PTR @ TYPE 34 EMIT EXIT THEN
  x VEC? IF
    35 EMIT 40 EMIT
    x PTR ARRAY-LEN 0 ?DO
      I 0> IF 32 EMIT THEN
      x PTR I ARRAY@ RECURSE
    LOOP
    41 EMIT EXIT
  THEN
  x PAIR? IF
    40 EMIT
    BEGIN x PAIR? WHILE
      x CAR RECURSE
      x CDR TO x
      x PAIR? x NIL <> AND IF 32 EMIT THEN
    REPEAT
    x NIL <> IF 32 EMIT 46 EMIT 32 EMIT x RECURSE THEN
    41 EMIT EXIT
  THEN
  63 EMIT ;

\ --------------------------------------------------------------------------
\ primitives (args is the Scheme argument list)
: P+ { a -- v } a CAR FIX> a CDR CAR FIX> + >FIX ;
: P- { a -- v } a CAR FIX> a CDR CAR FIX> - >FIX ;
: P* { a -- v } a CAR FIX> a CDR CAR FIX> * >FIX ;
: P/ { a -- v } a CAR FIX> a CDR CAR FIX> / >FIX ;
: P= { a -- v } a CAR FIX> a CDR CAR FIX> = IF STRUE ELSE SFALSE THEN ;
: P< { a -- v } a CAR FIX> a CDR CAR FIX> < IF STRUE ELSE SFALSE THEN ;
: PCONS { a -- v } a CAR a CDR CAR CONS ;
: PCAR { a -- v } a CAR CAR ;
: PCDR { a -- v } a CAR CDR ;
: PLIST { a -- v } a ;
: PNULL? { a -- v } a CAR NIL = IF STRUE ELSE SFALSE THEN ;
: PPAIR? { a -- v } a CAR PAIR? IF STRUE ELSE SFALSE THEN ;
: PNOT { a -- v } a CAR SFALSE = IF STRUE ELSE SFALSE THEN ;
: PEQ? { a -- v } a CAR a CDR CAR = IF STRUE ELSE SFALSE THEN ;
: PDISPLAY { a -- v } a CAR WRITE SVOID ;
: PNEWLINE { a -- v } CR SVOID ;
: PMAKEVEC { a -- v } a CAR FIX> VMAKE ;
: PVEC? { a -- v } a CAR VEC? IF STRUE ELSE SFALSE THEN ;
: PVLEN { a -- v } a CAR VLEN >FIX ;
: PVREF { a -- v } a CAR a CDR CAR FIX> VREF ;
: PVSET { a -- v } a CDR CDR CAR a CAR a CDR CAR FIX> VSET SVOID ;

CREATE PRIMS
' P+ , ' P- , ' P* , ' P/ , ' P= , ' P< ,
' PCONS , ' PCAR , ' PCDR , ' PLIST , ' PNULL? , ' PPAIR? ,
' PNOT , ' PEQ? , ' PDISPLAY , ' PNEWLINE ,
' PMAKEVEC , ' PVEC? , ' PVLEN , ' PVREF , ' PVSET ,

: PRIM ( idx -- obj ) 8 * T-PRIM OR ;

: MAKE-CLOSURE { params body env -- c }
  SCM-CLO SCM-STORE NEW TO c
  params c 0 FIELD!
  body   c 1 FIELD!
  env    c 2 FIELD!
  c T-CLO OR ;

\ --------------------------------------------------------------------------
\ evaluator: an iterative CEK-style machine.  The continuation is a plain
\ cell stack (KSTK), not heap objects, and is registered as a GC root range,
\ so Scheme recursion uses neither the C stack nor allocation.  Tail calls are
\ constant-space.
VARIABLE M-EXPR
VARIABLE M-ENV
VARIABLE M-VAL
VARIABLE M-WANT-VAL
VARIABLE M-DONE

0 CONSTANT K-IF
1 CONSTANT K-SEQ
2 CONSTANT K-OP
3 CONSTANT K-ARGS
4 CONSTANT K-SET
5 CONSTANT K-DEF
6 CONSTANT K-AND
7 CONSTANT K-OR
8 CONSTANT K-LET

1048576 CONSTANT KSTK-CAP
CREATE KSTK KSTK-CAP ALLOT
VARIABLE KSP
: K@ { i -- x } i CELLS KSTK + @ ;
: KPUSH { x -- }
  KSP @ KSTK-CAP >= IF -1 THROW THEN
  x KSP @ CELLS KSTK + !  1 KSP +! ;
: KPOP ( -- x )
  KSP @ 0= IF -1 THROW THEN
  KSP @ 1- KSP !  KSP @ K@ ;
: KTOP ( -- x ) KSP @ 1- K@ ;

: K2> { tag a b -- } a KPUSH b KPUSH tag KPUSH ;
: K3> { tag a b c -- } a KPUSH b KPUSH c KPUSH tag KPUSH ;
: K4> { tag a b c d -- } a KPUSH b KPUSH c KPUSH d KPUSH tag KPUSH ;
: K5> { tag a b c d e -- } a KPUSH b KPUSH c KPUSH d KPUSH e KPUSH tag KPUSH ;
: K6> { tag a b c d e f -- } a KPUSH b KPUSH c KPUSH d KPUSH e KPUSH f KPUSH tag KPUSH ;

: XCONS { l x -- l' } x l CONS ;
: REVERSE { l -- r } NIL TO r
  BEGIN l PAIR? WHILE r l CAR XCONS TO r l CDR TO l REPEAT r ;

: M-EVAL-NOW { expr env } expr M-EXPR ! env M-ENV ! 0 M-WANT-VAL ! ;
: M-RETURN { val } val M-VAL ! -1 M-WANT-VAL ! ;

: M-SEQ { forms env }
  forms PAIR? 0= IF NIL M-RETURN EXIT THEN
  forms CDR PAIR? 0= IF forms CAR env M-EVAL-NOW EXIT THEN
  K-SEQ forms CDR env K2>
  forms CAR env M-EVAL-NOW ;

: M-AND { forms env }
  forms PAIR? 0= IF STRUE M-RETURN EXIT THEN
  forms CDR PAIR? 0= IF forms CAR env M-EVAL-NOW EXIT THEN
  K-AND forms CDR env K2>
  forms CAR env M-EVAL-NOW ;

: M-OR { forms env }
  forms PAIR? 0= IF SFALSE M-RETURN EXIT THEN
  forms CDR PAIR? 0= IF forms CAR env M-EVAL-NOW EXIT THEN
  K-OR forms CDR env K2>
  forms CAR env M-EVAL-NOW ;

: M-LET-STEP { rest frame k env body -- bind }
  rest PAIR? 0= IF frame env CONS body SWAP M-SEQ EXIT THEN
  rest CAR TO bind
  K-LET rest CDR frame env body bind CAR k K6>
  bind CDR CAR env M-EVAL-NOW ;
: M-LET { binds env body -- frame } binds LIST-LEN 2 * SCM-STORE ARRAY-NEW TO frame
  binds frame 0 env body M-LET-STEP ;

: M-APPLY { proc args }
  proc CLO? IF
    proc PTR 1 FIELD@
    proc PTR 0 FIELD@ args proc PTR 2 FIELD@ ENV-EXTEND
    M-SEQ EXIT
  THEN
  proc PRIM? IF proc PTR PRIMS + @ args SWAP EXECUTE M-RETURN EXIT THEN
  SVOID M-RETURN ;

: M-EVAL-1 { -- expr op }
  M-EXPR @ TO expr
  expr PAIR? 0= IF
    expr SYM? IF expr M-ENV @ ENV-LOOKUP ELSE expr THEN M-RETURN EXIT
  THEN
  expr CAR SYM? IF expr CAR TO op ELSE 0 TO op THEN
  op 0= IF
    K-OP expr CDR M-ENV @ K2>
    expr CAR M-ENV @ M-EVAL-NOW EXIT
  THEN
  op SYM-QUOTE = IF expr CDR CAR M-RETURN EXIT THEN
  op SYM-LAMBDA = IF expr CDR CAR expr CDR CDR M-ENV @ MAKE-CLOSURE M-RETURN EXIT THEN
  op SYM-IF = IF
    K-IF expr CDR CDR CAR expr CDR CDR CDR CAR M-ENV @ K3>
    expr CDR CAR M-ENV @ M-EVAL-NOW EXIT
  THEN
  op SYM-BEGIN = IF expr CDR M-ENV @ M-SEQ EXIT THEN
  op SYM-LET = IF expr CDR CAR M-ENV @ expr CDR CDR M-LET EXIT THEN
  op SYM-DEFINE = IF
    expr CDR CAR PAIR? IF
      expr CDR CAR CDR expr CDR CDR M-ENV @ MAKE-CLOSURE
      expr CDR CAR CAR SWAP M-ENV @ ENV-DEFINE
      SVOID M-RETURN EXIT
    THEN
    K-DEF expr CDR CAR M-ENV @ K2>
    expr CDR CDR CAR M-ENV @ M-EVAL-NOW EXIT
  THEN
  op SYM-SET! = IF
    K-SET expr CDR CAR M-ENV @ K2>
    expr CDR CDR CAR M-ENV @ M-EVAL-NOW EXIT
  THEN
  op SYM-AND = IF expr CDR M-ENV @ M-AND EXIT THEN
  op SYM-OR = IF expr CDR M-ENV @ M-OR EXIT THEN
  K-OP expr CDR M-ENV @ K2>
  expr CAR M-ENV @ M-EVAL-NOW ;

: M-RESUME { -- tag a b c d e f pair }
  KSP @ 0= IF -1 M-DONE ! EXIT THEN
  KTOP TO tag
  tag K-IF = IF
    KPOP DROP  KPOP TO c  KPOP TO b  KPOP TO a
    M-VAL @ TRUTHY? IF a ELSE b THEN c M-EVAL-NOW EXIT
  THEN
  tag K-SEQ = IF
    KPOP DROP  KPOP TO b  KPOP TO a
    a CDR PAIR? 0= IF a CAR b M-EVAL-NOW EXIT THEN
    K-SEQ a CDR b K2>  a CAR b M-EVAL-NOW EXIT
  THEN
  tag K-OP = IF
    KPOP DROP  KPOP TO b  KPOP TO a
    a PAIR? 0= IF M-VAL @ NIL M-APPLY EXIT THEN
    K-ARGS M-VAL @ a CDR NIL NIL b K5>  a CAR b M-EVAL-NOW EXIT
  THEN
  tag K-ARGS = IF
    \ a=proc b=remaining c=head d=tail e=env ; append the just-evaluated arg
    KPOP DROP  KPOP TO e  KPOP TO d  KPOP TO c  KPOP TO b  KPOP TO a
    M-VAL @ NIL CONS TO pair
    c PAIR? 0= IF pair TO c pair TO d ELSE pair d SET-CDR pair TO d THEN
    b PAIR? 0= IF a c M-APPLY EXIT THEN
    K-ARGS a b CDR c d e K5>  b CAR e M-EVAL-NOW EXIT
  THEN
  tag K-SET = IF
    KPOP DROP  KPOP TO b  KPOP TO a
    a M-VAL @ b ENV-SET DROP SVOID M-RETURN EXIT
  THEN
  tag K-DEF = IF
    KPOP DROP  KPOP TO b  KPOP TO a
    a M-VAL @ b ENV-DEFINE SVOID M-RETURN EXIT
  THEN
  tag K-AND = IF
    M-VAL @ TRUTHY? 0= IF -1 M-WANT-VAL ! EXIT THEN
    KPOP DROP  KPOP TO b  KPOP TO a
    a CDR PAIR? 0= IF a CAR b M-EVAL-NOW EXIT THEN
    K-AND a CDR b K2>  a CAR b M-EVAL-NOW EXIT
  THEN
  tag K-OR = IF
    M-VAL @ TRUTHY? IF -1 M-WANT-VAL ! EXIT THEN
    KPOP DROP  KPOP TO b  KPOP TO a
    a CDR PAIR? 0= IF a CAR b M-EVAL-NOW EXIT THEN
    K-OR a CDR b K2>  a CAR b M-EVAL-NOW EXIT
  THEN
  tag K-LET = IF
    KPOP DROP  KPOP TO f  KPOP TO e  KPOP TO d  KPOP TO c  KPOP TO b  KPOP TO a
    e b f ARRAY!
    M-VAL @ b f 1+ ARRAY!
    a b f 2 + c d M-LET-STEP EXIT
  THEN
  -1 THROW ;

: (EVAL) { expr env }
  0 KSP !
  expr M-EXPR ! env M-ENV ! 0 M-WANT-VAL ! 0 M-DONE !
  BEGIN M-DONE @ 0= WHILE
    M-WANT-VAL @ IF M-RESUME ELSE M-EVAL-1 THEN
  REPEAT
  M-VAL @ ;
' (EVAL) IS EVAL

\ --------------------------------------------------------------------------
: BIND-PRIM { c-addr u idx -- sym } c-addr u SYM-INTERN TO sym
  idx PRIM sym ENV-GLOBAL HT-PUT ;
: SCHEME-INIT
  7 SCM-STORE STORE-MASK!
  \ per-store global bindings table, kept alive as a store root
  64 SCM-STORE HT-NEW SCM-STORE STORE-BINDINGS!
  SCM-STORE S-BINDINGS CELLS + SCM-STORE ROOT
  SYMS SCM-STORE ROOT
  M-EXPR SCM-STORE ROOT
  M-ENV  SCM-STORE ROOT
  M-VAL  SCM-STORE ROOT
  KSTK KSP SCM-STORE ROOT-RANGE
  S" +"       0 BIND-PRIM
  S" -"       1 BIND-PRIM
  S" *"       2 BIND-PRIM
  S" /"       3 BIND-PRIM
  S" ="       4 BIND-PRIM
  S" <"       5 BIND-PRIM
  S" cons"    6 BIND-PRIM
  S" car"     7 BIND-PRIM
  S" cdr"     8 BIND-PRIM
  S" list"    9 BIND-PRIM
  S" null?"  10 BIND-PRIM
  S" pair?"  11 BIND-PRIM
  S" not"    12 BIND-PRIM
  S" eq?"    13 BIND-PRIM
  S" display"14 BIND-PRIM
  S" newline"15 BIND-PRIM
  S" make-vector" 16 BIND-PRIM
  S" vector?"     17 BIND-PRIM
  S" vector-length" 18 BIND-PRIM
  S" vector-ref"  19 BIND-PRIM
  S" vector-set!" 20 BIND-PRIM ;

: SCHEME-EVAL { c-addr u -- }
  c-addr S-IN ! u S-LEN ! 0 S-POS !
  BEGIN S-SKIP S-END? 0= WHILE
    READ NIL EVAL WRITE CR
  REPEAT ;

\ interactive REPL: read a line, evaluate all forms, repeat until EOF
: SCHEME
  BEGIN
    ." scheme> "
    READ-LINE        ( a u )
    DUP 0> WHILE
      SCHEME-EVAL
    REPEAT
    2DROP ;

SCHEME-INIT
