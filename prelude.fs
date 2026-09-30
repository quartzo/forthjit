\ prelude.jit - native prelude, assembled to machine code at boot.
\ Words here are CODE definitions (IR); the C kernel is kept minimal.

\ ---- stack --------------------------------------------------------------

CODE DUP
  require 1
  room 1
  mov R0, [S1-8]
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE

CODE DROP
  require 1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE SWAP
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  mov [S1-8], R1
  mov [S1-16], R0
  mov R0, S1
;CODE

CODE OVER
  require 2
  room 1
  mov R0, [S1-16]
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE

CODE NIP
  require 2
  mov R0, [S1-8]
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE TUCK
  require 2
  room 1
  mov R0, [S1-8]
  mov R1, [S1-16]
  mov [S1-16], R0
  mov [S1-8], R1
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE

CODE ROT
  require 3
  mov R0, [S1-8]
  mov R1, [S1-16]
  mov R2, [S1-24]
  mov [S1-24], R1
  mov [S1-16], R0
  mov [S1-8], R2
  mov R0, S1
;CODE

CODE 2DUP
  require 2
  room 2
  inline OVER
  inline OVER
;CODE

CODE 2DROP
  require 2
  inline DROP
  inline DROP
;CODE

\ ---- arithmetic ----------------------------------------------------------

CODE +
  require 2
  mov R0, [S1-8]
  add R0, R0, [S1-16]
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE -
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  sub R1, R1, R0
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE *
  require 2
  mov R0, [S1-8]
  mul R0, R0, [S1-16]
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

\ ---- arithmetic / logic --------------------------------------------------

CODE NEGATE
  require 1
  mov R0, [S1-8]
  neg R0, R0
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE ABS
  require 1
  mov R0, [S1-8]
  cmp ge R0, #0, @done
  neg R0, R0
label: done
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE 1+
  require 1
  mov R0, [S1-8]
  add R0, R0, #1
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE 1-
  require 1
  mov R0, [S1-8]
  add R0, R0, #-1
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE MIN
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  cmp lt R0, R1, @b_smaller
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
  ret S1
label: b_smaller
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE MAX
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  cmp gt R0, R1, @b_bigger
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
  ret S1
label: b_bigger
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE AND
  require 2
  mov R0, [S1-8]
  and R0, R0, [S1-16]
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE OR
  require 2
  mov R0, [S1-8]
  or R0, R0, [S1-16]
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE XOR
  require 2
  mov R0, [S1-8]
  xor R0, R0, [S1-16]
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE INVERT
  require 1
  mov R0, [S1-8]
  not R0, R0
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE LSHIFT
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  shl R1, R1, R0
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE RSHIFT
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  lshr R1, R1, R0
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

\ ---- comparisons (-1 / 0) ------------------------------------------------

CODE =
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  setflags eq R1, R0
  flags eq R1
  neg R1, R1
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE <>
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  setflags ne R1, R0
  flags ne R1
  neg R1, R1
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE <
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  setflags lt R1, R0
  flags lt R1
  neg R1, R1
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE >
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  setflags gt R1, R0
  flags gt R1
  neg R1, R1
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE <=
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  setflags le R1, R0
  flags le R1
  neg R1, R1
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE >=
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  setflags ge R1, R0
  flags ge R1
  neg R1, R1
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE 0=
  require 1
  mov R0, [S1-8]
  setflags eq R0, #0
  flags eq R0
  neg R0, R0
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE 0<
  require 1
  mov R0, [S1-8]
  setflags lt R0, #0
  flags lt R0
  neg R0, R0
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE 0>
  require 1
  mov R0, [S1-8]
  setflags gt R0, #0
  flags gt R0
  neg R0, R0
  mov [S1-8], R0
  mov R0, S1
;CODE

\ ---- misc ---------------------------------------------------------------

CODE ?DUP
  require 1
  room 1
  mov R0, [S1-8]
  cmp zero R0, #0, @done
  mov [S1], R0
  add S1, S1, #8
label: done
  mov R0, S1
;CODE

CODE @
  require 1
  mov R0, [S1-8]
  mov R1, [R0]
  mov [S1-8], R1
  mov R0, S1
;CODE

CODE !
  require 2
  mov R0, [S1-8]
  mov R1, [S1-16]
  mov [R0], R1
  add S1, S1, #-16
  mov R0, S1
;CODE

\ ---- output -------------------------------------------------------------

CODE EMIT
  require 1
  mov R0, [S1-8]
  add S1, S1, #-8
  sig W 32
  icall &putchar
  mov R0, S1
;CODE

CODE CR
  require 0
  mov R0, #10
  sig W 32
  icall &putchar
  mov R0, S1
;CODE

\ ---- data space / depth --------------------------------------------------

CODE DEPTH
  require 0
  room 1
  mov R0, &dstack
  mov R1, S1
  sub R1, R1, R0
  lshr R1, R1, #3
  mov [S1], R1
  add S1, S1, #8
  mov R0, S1
;CODE

CODE ,
  require 1
  mov R0, &memtop
  mov.u32 R1, [R0]
  mov R2, &mem
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [S1-8]
  mov [R2], R1
  mov.u32 R1, [R0]
  add R1, R1, #1
  mov32 [R0], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE ALLOT
  require 1
  mov R0, &memtop
  mov.u32 R1, [R0]
  mov R2, [S1-8]
  add R1, R1, R2
  mov32 [R0], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

\ ---- division and loop indices -------------------------------------------

CODE /
  require 2
  mov R1, [S1-8]
  cmp eq R1, #0, @zero
  mov R0, [S1-16]
  mov R1, [S1-8]
  op0 SLJIT_DIVMOD_SW
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
  ret S1
label: zero
  sig V
  icall &forth_divzero
;CODE

CODE MOD
  require 2
  mov R1, [S1-8]
  cmp eq R1, #0, @zero
  mov R0, [S1-16]
  mov R1, [S1-8]
  op0 SLJIT_DIVMOD_SW
  mov [S1-16], R1
  add S1, S1, #-8
  mov R0, S1
  ret S1
label: zero
  sig V
  icall &forth_divzero
;CODE

CODE I
  require 0
  room 1
  mov R0, &rp
  mov.u32 R1, [R0]
  cmp lt R1, #1, @bad
  mov.u32 R1, [R0]
  mov R2, &rstack
  add R1, R1, #-1
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [R2]
  mov [S1], R1
  add S1, S1, #8
  mov R0, S1
  ret S1
label: bad
  mov R0, #-256
  sig V W
  icall &forth_raise
;CODE

CODE J
  require 0
  room 1
  mov R0, &rp
  mov.u32 R1, [R0]
  cmp lt R1, #3, @bad
  mov.u32 R1, [R0]
  mov R2, &rstack
  add R1, R1, #-3
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [R2]
  mov [S1], R1
  add S1, S1, #8
  mov R0, S1
  ret S1
label: bad
  mov R0, #-256
  sig V W
  icall &forth_raise
;CODE

\ ---- dictionary access (over the C helpers) ------------------------------

1 CONSTANT F_IMMEDIATE
2 CONSTANT F_HIDDEN

CODE LATEST
  require 0
  room 1
  sig P
  icall &forth_latest
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE

CODE >LINK
  require 1
  mov R0, [S1-8]
  sig P P
  icall &forth_link
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE >NAME
  require 1
  room 1
  mov R0, [S1-8]
  sig P P
  icall &forth_name
  mov S0, R0
  mov R0, S0
  sig W P
  icall &strlen
  mov [S1-8], S0
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE

CODE WORD-DATA
  require 1
  mov R0, [S1-8]
  sig W P
  icall &forth_data
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE >BODY
  require 1
  mov R0, [S1-8]
  sig P P
  icall &forth_body
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE IMMEDIATE?
  require 1
  mov R0, [S1-8]
  sig W P
  icall &forth_immediate
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE HIDDEN?
  require 1
  mov R0, [S1-8]
  sig W P
  icall &forth_hidden
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE COLON?
  require 1
  mov R0, [S1-8]
  sig W P
  icall &forth_colon_p
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE NATIVE?
  require 1
  mov R0, [S1-8]
  sig W P
  icall &forth_native_p
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE VARIABLE?
  require 1
  mov R0, [S1-8]
  sig W P
  icall &forth_variable_p
  mov [S1-8], R0
  mov R0, S1
;CODE

CODE FIND
  require 2
  mov R0, [S1-16]
  mov R1, [S1-8]
  sig P P 32
  icall &forth_find
  mov [S1-16], R0
  add S1, S1, #-8
  mov R0, S1
;CODE

\ ---- return stack, exit, stack addresses, string output -------------------

CODE >R
  require 1
  mov R0, &rp
  mov.u32 R1, [R0]
  mov R2, &rstack
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [S1-8]
  mov [R2], R1
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #1
  mov32 [R0], R1
  add S1, S1, #-8
  mov R0, S1
;CODE

CODE R>
  room 1
  mov R0, &rp
  mov.u32 R1, [R0]
  mov R2, #1
  mov R0, R1
  mov R1, R2
  sig V W W
  icall &forth_need
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #-1
  mov32 [R0], R1
  mov R2, &rstack
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [R2]
  mov [S1], R1
  add S1, S1, #8
  mov R0, S1
;CODE

CODE R@
  room 1
  mov R0, &rp
  mov.u32 R1, [R0]
  mov R2, #1
  mov R0, R1
  mov R1, R2
  sig V W W
  icall &forth_need
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #-1
  mov R2, &rstack
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [R2]
  mov [S1], R1
  add S1, S1, #8
  mov R0, S1
;CODE

CODE 2>R
  require 2
  mov R0, &rp
  mov.u32 R1, [R0]
  shl R1, R1, #3
  mov R2, &rstack
  add R2, R2, R1
  mov R1, [S1-16]
  mov [R2], R1
  mov R1, [S1-8]
  mov [R2+8], R1
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #2
  mov32 [R0], R1
  add S1, S1, #-16
  mov R0, S1
;CODE

CODE 2R>
  room 2
  mov R0, &rp
  mov.u32 R1, [R0]
  mov R2, #2
  mov R0, R1
  mov R1, R2
  sig V W W
  icall &forth_need
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #-2
  mov32 [R0], R1
  shl R1, R1, #3
  mov R2, &rstack
  add R2, R2, R1
  mov R1, [R2]
  mov R3, [R2+8]
  mov [S1], R1
  mov [S1+8], R3
  add S1, S1, #16
  mov R0, S1
;CODE

CODE EXIT
  mov R0, &rp
  mov.u32 R1, [R0]
  mov R2, #1
  mov R0, R1
  mov R1, R2
  sig V W W
  icall &forth_need
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #-1
  mov32 [R0], R1
  mov R2, &rstack
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [R2]
  mov R2, &ip
  mov [R2], R1
  mov R0, S1
;CODE

CODE SP@
  require 1
  room 1
  mov R0, S1
  sub R0, R0, #8
  mov [S1], R0
  add S1, S1, #8
  mov R0, S1
;CODE

CODE SP!
  require 1
  mov R0, [S1-8]
  mov R1, &dstack
  sub R0, R0, R1
  lshr R0, R0, #3
  add R0, R0, #1
  shl R0, R0, #3
  mov R1, &dstack
  add R0, R1, R0
;CODE

CODE RP@
  room 1
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #-1
  mov R2, &rstack
  shl R1, R1, #3
  add R2, R2, R1
  mov [S1], R2
  add S1, S1, #8
  mov R0, S1
;CODE

CODE TYPE
  require 2
  mov R0, [S1-16]
  mov R1, [S1-8]
  add S1, S1, #-16
  sig V P W
  icall &forth_type
  mov R0, S1
;CODE

\ ---- number output (threaded Forth) --------------------------------------

8 CONSTANT CELL-SIZE
: CELLS  CELL-SIZE * ;
: +!     ( n addr -- ) DUP @ ROT + SWAP ! ;

CODE PICK
  require 1
  mov R0, [S1-8]
  add R0, R0, #2
  mov R1, S1
  mov R2, &dstack
  sub R1, R1, R2
  lshr R1, R1, #3
  mov R2, R0
  mov R0, R1
  mov R1, R2
  sig V W W
  icall &forth_need
  mov R0, [S1-8]
  add R0, R0, #2
  shl R0, R0, #3
  mov R1, S1
  sub R1, R1, R0
  mov R2, [R1]
  mov [S1-8], R2
  mov R0, S1
;CODE

: SPACE 32 EMIT ;

VARIABLE NUMCNT
VARIABLE IBUF  256 ALLOT

: . ( n -- )
  DUP 0< IF 45 EMIT NEGATE THEN
  DUP 0= IF DROP 48 EMIT SPACE EXIT THEN
  0 NUMCNT !
  BEGIN
    DUP 10 MOD 48 +
    NUMCNT @ CELLS IBUF + !
    1 NUMCNT +!
    10 /
    DUP 0=
  UNTIL
  DROP
  BEGIN NUMCNT @ 0> WHILE
    -1 NUMCNT +!
    NUMCNT @ CELLS IBUF + @ EMIT
  REPEAT
  SPACE ;

VARIABLE SCNT
: .S ( -- )
  60 EMIT DEPTH . 62 EMIT
  0 SCNT !
  BEGIN DEPTH SCNT @ > WHILE
    DEPTH 1- SCNT @ - PICK .
    1 SCNT +!
  REPEAT
  CR ;

\ ---- control flow defined in Forth (over the compiler kit) ---------------

: PATCH ( target at -- )  OVER OVER - 1-  SWAP CODE! DROP ;

: IF    ['] (0branch) COMPILE,  HERE  0 CODE, ; IMMEDIATE
: THEN  HERE SWAP PATCH ; IMMEDIATE
: ELSE  ['] (branch) COMPILE,  HERE 0 CODE,  SWAP HERE SWAP PATCH ; IMMEDIATE

: BEGIN  HERE ; IMMEDIATE
: UNTIL  ['] (0branch) COMPILE,  HERE 0 CODE,  PATCH ; IMMEDIATE
: AGAIN  ['] (branch) COMPILE,  HERE 0 CODE,  PATCH ; IMMEDIATE
: WHILE  ['] (0branch) COMPILE,  HERE 0 CODE, ; IMMEDIATE
: REPEAT ['] (branch) COMPILE,  HERE 0 CODE,  ROT SWAP PATCH  HERE SWAP PATCH ; IMMEDIATE

: [  0 STATE ! ; IMMEDIATE
: ]  1 STATE ! ; IMMEDIATE
: LITERAL  ['] (lit) COMPILE,  CODE, ; IMMEDIATE

\ ---- defining words in Forth ---------------------------------------------

: VARIABLE CREATE 1 CELLS ALLOT ;
: CONSTANT CREATE , DOES> @ ;

\ ---- loop control --------------------------------------------------------

CODE UNLOOP
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #-2
  mov32 [R0], R1
  mov R0, S1
;CODE

\ ---- tooling -------------------------------------------------------------

CODE NATIVE>
  require 1
  mov R0, [S1-8]
  sig P P
  icall &forth_native
  mov [S1-8], R0
  mov R0, S1
;CODE

: JIT? ( xt -- flag )  DUP COLON? SWAP NATIVE? AND ;

: H>CH { n -- c }  n 9 > IF n 87 + ELSE n 48 + THEN ;
: H.   { n -- }    n 16 / H>CH EMIT  n 15 AND H>CH EMIT SPACE ;

: DUMP { a u -- c }        \ hex dump, 16 bytes per line
  BEGIN u 0> WHILE
    a C@ H.
    a 1+ TO a
    u 1- TO u
    c 1+ TO c
    c 16 MOD 0= IF CR THEN
  REPEAT CR ;

: PREFIX? { a u pa pu -- f }
  u pu < IF 0 EXIT THEN
  a pu pa pu COMPARE 0= ;

: WORDS-MATCH { a u -- c }  \ list words whose name starts with ( a u )
  LATEST
  BEGIN DUP WHILE
    DUP HIDDEN? 0= IF
      DUP >NAME a u PREFIX? IF
        DUP >NAME TYPE SPACE
        c 1+ TO c
        c 5 MOD 0= IF CR THEN
      THEN
    THEN
    >LINK
  REPEAT DROP CR ;
