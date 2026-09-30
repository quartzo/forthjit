\ lib/scheme.fs - a minimal Scheme, loaded with `REQUIRE scheme.fs`.
\ Values are tagged cells (low 3 bits):
\   0 fixnum   1 pair   2 symbol   3 string   4 closure   5 primitive
\   7 specials: NIL(7) SFALSE(15) STRUE(23) SVOID(31)
\ Symbols are interned; pairs/closures/strings live on the Arena heap.

\ --------------------------------------------------------------------------
0 CONSTANT T-FIX
1 CONSTANT T-PAIR
2 CONSTANT T-SYM
3 CONSTANT T-STR
4 CONSTANT T-CLO
5 CONSTANT T-PRIM
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
: TRUTHY? SFALSE <> ;

: CONS ( a d -- p ) 16 ALLOC >R R@ 8 + ! R@ ! R> T-PAIR OR ;
: CAR ( p -- a ) PTR @ ;
: CDR ( p -- d ) PTR 8 + @ ;
: SET-CAR ( x p -- ) PTR ! ;
: SET-CDR ( x p -- ) PTR 8 + ! ;

DEFER EVAL

\ --------------------------------------------------------------------------
\ symbols
VARIABLE SYMS
: SYM-LEN PTR @ ;
: SYM-DATA PTR 8 + ;
: STREQ? { a1 a2 n -- f }
  n 0 ?DO a1 I + C@ a2 I + C@ <> IF 0 UNLOOP EXIT THEN LOOP -1 ;
: SYM-MAKE { c-addr u -- s }
  8 u + ALLOC TO s
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
  8 u + ALLOC TO s
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
\ environments: alist of frames; a frame is an alist of (sym . val)
: FRAME-LOOKUP { sym frame -- val }
  frame
  BEGIN DUP PAIR? WHILE
    DUP CAR CAR sym = IF CAR CDR EXIT THEN
    CDR
  REPEAT DROP 0 ;
: ENV-LOOKUP { sym env -- val }
  env
  BEGIN DUP PAIR? WHILE
    sym OVER CAR FRAME-LOOKUP
    DUP IF NIP EXIT THEN DROP
    CDR
  REPEAT DROP 0 ;
: ENV-DEFINE { sym val env -- }
  sym val CONS env CAR CONS env SET-CAR ;
: ENV-SET { sym val env -- frame }
  env
  BEGIN DUP PAIR? WHILE
    DUP CAR TO frame
    frame
    BEGIN DUP PAIR? WHILE
      DUP CAR CAR sym = IF DUP CAR val SWAP SET-CDR DROP -1 EXIT THEN
      CDR
    REPEAT DROP
    CDR
  REPEAT DROP 0 ;
: ENV-EXTEND { syms vals env -- f }
  BEGIN syms PAIR? WHILE
    syms CAR vals CAR CONS f CONS TO f
    syms CDR TO syms
    vals CDR TO vals
  REPEAT
  f env CONS ;
: LET-BIND { binds env -- f }
  BEGIN binds PAIR? WHILE
    binds CAR CAR
    binds CAR CDR CAR env EVAL
    CONS f CONS TO f
    binds CDR TO binds
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

CREATE PRIMS
' P+ , ' P- , ' P* , ' P/ , ' P= , ' P< ,
' PCONS , ' PCAR , ' PCDR , ' PLIST , ' PNULL? , ' PPAIR? ,
' PNOT , ' PEQ? , ' PDISPLAY , ' PNEWLINE ,

: PRIM ( idx -- obj ) 8 * T-PRIM OR ;

: MAKE-CLOSURE { params body env -- c }
  24 ALLOC TO c
  params c !
  body   c 8 + !
  env    c 16 + !
  c T-CLO OR ;

\ --------------------------------------------------------------------------
\ evaluator (EVAL is deferred; (EVAL) is installed at the end)
: EVAL-ARGS { list env -- vals }
  BEGIN list PAIR? WHILE
    list CAR env EVAL
    list CDR env RECURSE
    CONS EXIT
  REPEAT NIL ;
: EVAL-BODY { body env -- val }
  BEGIN body CDR PAIR? WHILE body CAR env EVAL DROP body CDR TO body REPEAT
  body CAR env EVAL ;
: EVAL-AND { list env -- v }
  STRUE TO v
  BEGIN list PAIR? WHILE
    list CAR env EVAL TO v
    v TRUTHY? 0= IF v EXIT THEN
    list CDR TO list
  REPEAT v ;
: EVAL-OR { list env -- val }
  BEGIN list PAIR? WHILE
    list CAR env EVAL DUP TRUTHY? IF EXIT THEN DROP
    list CDR TO list
  REPEAT SFALSE ;
: APPLY { proc args -- val }
  proc CLO? IF
    proc PTR CAR
    args
    proc PTR 16 + @
    ENV-EXTEND
    proc PTR 8 + @
    SWAP
    EVAL-BODY EXIT
  THEN
  proc PRIM? IF proc PTR PRIMS + @ args SWAP EXECUTE EXIT THEN
  SVOID ;
: (EVAL) { expr env -- op nm }
  expr PAIR? IF
    expr CAR SYM? IF expr CAR TO op ELSE 0 TO op THEN
  ELSE 0 TO op THEN
  expr PAIR? 0= IF
    expr SYM? IF expr env ENV-LOOKUP EXIT THEN
    expr EXIT
  THEN
  op IF
    op SYM-QUOTE = IF expr CDR CAR EXIT THEN
    op SYM-IF = IF
      expr CDR CAR env EVAL TRUTHY? IF
        expr CDR CDR CAR env EVAL
      ELSE
        expr CDR CDR CDR CAR env EVAL
      THEN EXIT
    THEN
    op SYM-LAMBDA = IF expr CDR CAR expr CDR CDR env MAKE-CLOSURE EXIT THEN
    op SYM-BEGIN = IF expr CDR env EVAL-BODY EXIT THEN
    op SYM-LET = IF expr CDR CDR expr CDR CAR env LET-BIND EVAL-BODY EXIT THEN
    op SYM-DEFINE = IF
      expr CDR CAR PAIR? IF
        expr CDR CAR CAR TO nm
        expr CDR CAR CDR
        expr CDR CDR
        env MAKE-CLOSURE
        nm SWAP env ENV-DEFINE SVOID EXIT
      THEN
      expr CDR CAR expr CDR CDR CAR env EVAL env ENV-DEFINE SVOID EXIT
    THEN
    op SYM-SET! = IF
      expr CDR CAR expr CDR CDR CAR env EVAL env ENV-SET DROP SVOID EXIT
    THEN
    op SYM-AND = IF expr CDR env EVAL-AND EXIT THEN
    op SYM-OR = IF expr CDR env EVAL-OR EXIT THEN
  THEN
  expr CAR env EVAL
  expr CDR env EVAL-ARGS
  APPLY ;
' (EVAL) IS EVAL

\ --------------------------------------------------------------------------
VARIABLE GENV
: BIND-PRIM { c-addr u idx -- } c-addr u SYM-INTERN idx PRIM GENV @ ENV-DEFINE ;
: SCHEME-INIT
  NIL NIL CONS GENV !
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
  S" newline"15 BIND-PRIM ;

: SCHEME-EVAL { c-addr u -- }
  c-addr S-IN ! u S-LEN ! 0 S-POS !
  BEGIN S-SKIP S-END? 0= WHILE
    READ GENV @ EVAL WRITE CR
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
