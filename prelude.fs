\ prelude.jit - native prelude, assembled to machine code at boot.
\ Words here are CODE definitions (IR); the C kernel is kept minimal.

\ ---- stack --------------------------------------------------------------

CODE DUP
  require 1
  room 1
  mov R0, [S1-1c]
  spush R0
;CODE

CODE DROP
  require 1
  add S1, S1, #-8
;CODE

CODE SWAP
  require 2
  spop R0
  spop R1
  spush R0
  spush R1
;CODE

CODE OVER
  require 2
  room 1
  mov R0, [S1-2c]
  spush R0
;CODE

CODE NIP
  require 2
  mov R0, [S1-1c]
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

CODE TUCK
  require 2
  room 1
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  mov [S1-2c], R0
  mov [S1-1c], R1
  mov [S1], R0
  add S1, S1, #8
;CODE

CODE ROT
  require 3
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  mov R2, [S1-3c]
  mov [S1-3c], R1
  mov [S1-2c], R0
  mov [S1-1c], R2
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
  mov R0, [S1-1c]
  add R0, R0, [S1-2c]
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

CODE -
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  sub R1, R1, R0
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE *
  require 2
  mov R0, [S1-1c]
  mul R0, R0, [S1-2c]
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

\ ---- arithmetic / logic --------------------------------------------------

CODE NEGATE
  require 1
  mov R0, [S1-1c]
  neg R0, R0
  mov [S1-1c], R0
;CODE

CODE ABS
  require 1
  mov R0, [S1-1c]
  cmp ge R0, #0, @done
  neg R0, R0
label: done
  mov [S1-1c], R0
;CODE

CODE 1+
  require 1
  mov R0, [S1-1c]
  add R0, R0, #1
  mov [S1-1c], R0
;CODE

CODE 1-
  require 1
  mov R0, [S1-1c]
  add R0, R0, #-1
  mov [S1-1c], R0
;CODE

CODE MIN
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  cmp lt R0, R1, @b_smaller
  mov [S1-2c], R1
  add S1, S1, #-8
  ret S1
label: b_smaller
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

CODE MAX
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  cmp gt R0, R1, @b_bigger
  mov [S1-2c], R1
  add S1, S1, #-8
  ret S1
label: b_bigger
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

CODE AND
  require 2
  mov R0, [S1-1c]
  and R0, R0, [S1-2c]
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

CODE OR
  require 2
  mov R0, [S1-1c]
  or R0, R0, [S1-2c]
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

CODE XOR
  require 2
  mov R0, [S1-1c]
  xor R0, R0, [S1-2c]
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

CODE INVERT
  require 1
  mov R0, [S1-1c]
  not R0, R0
  mov [S1-1c], R0
;CODE

CODE LSHIFT
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  shl R1, R1, R0
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE RSHIFT
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  lshr R1, R1, R0
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

\ ---- comparisons (-1 / 0) ------------------------------------------------

CODE =
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  setflags eq R1, R0
  flags eq R1
  neg R1, R1
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE <>
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  setflags ne R1, R0
  flags ne R1
  neg R1, R1
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE <
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  setflags lt R1, R0
  flags lt R1
  neg R1, R1
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE >
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  setflags gt R1, R0
  flags gt R1
  neg R1, R1
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE <=
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  setflags le R1, R0
  flags le R1
  neg R1, R1
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE >=
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  setflags ge R1, R0
  flags ge R1
  neg R1, R1
  mov [S1-2c], R1
  add S1, S1, #-8
;CODE

CODE 0=
  require 1
  mov R0, [S1-1c]
  setflags eq R0, #0
  flags eq R0
  neg R0, R0
  mov [S1-1c], R0
;CODE

CODE 0<
  require 1
  mov R0, [S1-1c]
  setflags lt R0, #0
  flags lt R0
  neg R0, R0
  mov [S1-1c], R0
;CODE

CODE 0>
  require 1
  mov R0, [S1-1c]
  setflags gt R0, #0
  flags gt R0
  neg R0, R0
  mov [S1-1c], R0
;CODE

\ ---- misc ---------------------------------------------------------------

CODE ?DUP
  require 1
  room 1
  mov R0, [S1-1c]
  cmp zero R0, #0, @done
  mov [S1], R0
  add S1, S1, #8
label: done
;CODE

CODE @
  require 1
  mov R0, [S1-1c]
  mov R1, [R0]
  mov [S1-1c], R1
;CODE

CODE !
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  mov [R0], R1
  add S1, S1, #-16
;CODE

\ ---- output -------------------------------------------------------------

CODE EMIT
  require 1
  mov R0, [S1-1c]
  add S1, S1, #-8
  sig W 32
  icall &putchar
;CODE

CODE CR
  require 0
  mov R0, #10
  sig W 32
  icall &putchar
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
;CODE

\ `,` and `ALLOT` are C primitives over the reserved data region.

\ ---- code space access (native) ------------------------------------------

CODE CODE@
  require 1
  mov R0, [S1-1c]
  mov R1, &code
  mov R1, [R1]
  shl R0, R0, #3
  add R1, R1, R0
  mov R0, [R1]
  mov [S1-1c], R0
;CODE

CODE CODE!
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  mov R2, &code
  mov R2, [R2]
  shl R0, R0, #3
  add R2, R2, R0
  mov [R2], R1
  add S1, S1, #-16
;CODE

CODE HERE
  require 0
  room 1
  mov R0, &here
  mov R0, [R0]
  mov [S1], R0
  add S1, S1, #8
;CODE

CODE STATE
  require 0
  room 1
  mov R0, &state
  mov [S1], R0
  add S1, S1, #8
;CODE

\ ---- compiler kit: control flow and defining words in Forth --------------
\ (placed early so the whole prelude can use them; the C prims are gone)

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

: CONSTANT CREATE , DOES> @ ;

\ ---- CASE (over IF/ELSE/THEN, same data-stack control flow) --------------

: CASE    0 ; IMMEDIATE
: OF      POSTPONE OVER POSTPONE = POSTPONE IF POSTPONE DROP ; IMMEDIATE
: ENDOF   POSTPONE ELSE ; IMMEDIATE
: ENDCASE POSTPONE DROP  BEGIN ?DUP WHILE POSTPONE THEN REPEAT ; IMMEDIATE

\ ---- counted loops (compile side; runtime is native) ---------------------
\ Bookkeeping lives in two Forth-side stacks so IF/THEN (data stack) do not
\ collide with LEAVE.  BSTK holds [begin qdo] per loop (qdo = -1 if none);
\ LSTK holds [-1  leave/exit addrs ...] per loop.

CREATE BSTK 512 ALLOT
CREATE LSTK 512 ALLOT
CREATE BP 8 ALLOT
CREATE LP 8 ALLOT
CREATE LEND 8 ALLOT
CREATE LBEG 8 ALLOT
CREATE LQDO 8 ALLOT
0 BP !  0 LP !

: BPUSH ( x -- )   BP @ 8 * BSTK + !   BP @ 1+ BP ! ;
: BPOP  ( -- x )   BP @ 1- BP !   BP @ 8 * BSTK + @ ;
: LPUSH ( x -- )   LP @ 8 * LSTK + !   LP @ 1+ LP ! ;
: LPOP  ( -- x )   LP @ 1- LP !   LP @ 8 * LSTK + @ ;

: (CLOSE)   ( endword-xt -- )
  COMPILE,
  HERE LEND !  0 CODE,
  BPOP LQDO !
  BPOP LBEG !
  LBEG @  LEND @  PATCH
  BEGIN LPOP DUP -1 = 0= WHILE
    LEND @ 1+ SWAP PATCH
  REPEAT DROP
  LQDO @ -1 = 0= IF  LEND @ 1+ LQDO @ PATCH  THEN ;

: DO     ['] (do)     COMPILE,  HERE BPUSH  -1 BPUSH  -1 LPUSH ; IMMEDIATE
: ?DO    ['] (?do)  COMPILE,  HERE 0 CODE,  HERE BPUSH  BPUSH  -1 LPUSH ; IMMEDIATE
: LEAVE  ['] (leave)  COMPILE,  HERE 0 CODE,  LPUSH ; IMMEDIATE
: LOOP   ['] (loop)   (CLOSE) ; IMMEDIATE
: +LOOP  ['] (+loop)  (CLOSE) ; IMMEDIATE

\ ---- division and loop indices -------------------------------------------

CODE /
  require 2
  mov R1, [S1-1c]
  cmp eq R1, #0, @zero
  mov R0, [S1-2c]
  mov R1, [S1-1c]
  op0 SLJIT_DIVMOD_SW
  mov [S1-2c], R0
  add S1, S1, #-8
  ret S1
label: zero
  sig V
  icall &forth_divzero
;CODE

CODE MOD
  require 2
  mov R1, [S1-1c]
  cmp eq R1, #0, @zero
  mov R0, [S1-2c]
  mov R1, [S1-1c]
  op0 SLJIT_DIVMOD_SW
  mov [S1-2c], R1
  add S1, S1, #-8
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
;CODE

CODE >LINK
  require 1
  mov R0, [S1-1c]
  sig P P
  icall &forth_link
  mov [S1-1c], R0
;CODE

CODE >NAME
  require 1
  room 1
  mov R0, [S1-1c]
  sig P P
  icall &forth_name
  mov S0, R0
  mov R0, S0
  sig W P
  icall &strlen
  mov [S1-1c], S0
  mov [S1], R0
  add S1, S1, #8
;CODE

CODE WORD-DATA
  require 1
  mov R0, [S1-1c]
  sig W P
  icall &forth_data
  mov [S1-1c], R0
;CODE

CODE >BODY
  require 1
  mov R0, [S1-1c]
  sig P P
  icall &forth_body
  mov [S1-1c], R0
;CODE

CODE IMMEDIATE?
  require 1
  mov R0, [S1-1c]
  sig W P
  icall &forth_immediate
  mov [S1-1c], R0
;CODE

CODE HIDDEN?
  require 1
  mov R0, [S1-1c]
  sig W P
  icall &forth_hidden
  mov [S1-1c], R0
;CODE

CODE COLON?
  require 1
  mov R0, [S1-1c]
  sig W P
  icall &forth_colon_p
  mov [S1-1c], R0
;CODE

CODE NATIVE?
  require 1
  mov R0, [S1-1c]
  sig W P
  icall &forth_native_p
  mov [S1-1c], R0
;CODE

CODE VARIABLE?
  require 1
  mov R0, [S1-1c]
  sig W P
  icall &forth_variable_p
  mov [S1-1c], R0
;CODE

CODE FIND
  require 2
  mov R0, [S1-2c]
  mov R1, [S1-1c]
  sig P P 32
  icall &forth_find
  mov [S1-2c], R0
  add S1, S1, #-8
;CODE

\ ---- return stack, exit, stack addresses, string output -------------------

CODE >R
  require 1
  mov R0, &rp
  mov.u32 R1, [R0]
  mov R2, &rstack
  shl R1, R1, #3
  add R2, R2, R1
  mov R1, [S1-1c]
  mov [R2], R1
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #1
  mov32 [R0], R1
  add S1, S1, #-8
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
;CODE

CODE 2>R
  require 2
  mov R0, &rp
  mov.u32 R1, [R0]
  shl R1, R1, #3
  mov R2, &rstack
  add R2, R2, R1
  mov R1, [S1-2c]
  mov [R2], R1
  mov R1, [S1-1c]
  mov [R2+1c], R1
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #2
  mov32 [R0], R1
  add S1, S1, #-16
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
  mov R3, [R2+1c]
  mov [S1], R1
  mov [S1+1c], R3
  add S1, S1, #16
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
;CODE

CODE SP@
  require 1
  room 1
  mov R0, S1
  sub R0, R0, #8
  mov [S1], R0
  add S1, S1, #8
;CODE

CODE SP!
  require 1
  mov R0, [S1-1c]
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
;CODE

CODE TYPE
  require 2
  mov R0, [S1-2c]
  mov R1, [S1-1c]
  add S1, S1, #-16
  sig V P W
  icall &forth_type
;CODE

\ ---- byte memory and strings ---------------------------------------------

CODE C@
  require 1
  mov R0, [S1-1c]
  mov.u8 R0, [R0]
  mov [S1-1c], R0
;CODE

CODE C!
  require 2
  mov R0, [S1-1c]
  mov R1, [S1-2c]
  mov.u8 [R0], R1
  add S1, S1, #-16
;CODE

: FILL      { a u ch -- }  u 0 ?DO ch a I + C! LOOP ;
: CMOVE     { src dst u -- }  u 0 ?DO src I + C@ dst I + C! LOOP ;
: CMOVE>    { src dst u -- }  u 0 ?DO src u I - 1- + C@ dst u I - 1- + C! LOOP ;
: MOVE      { src dst u -- }
  src dst < IF  u 0 ?DO src I + C@ dst I + C! LOOP
            ELSE u 0 ?DO src u I - 1- + C@ dst u I - 1- + C! LOOP THEN ;
: COMPARE   { a1 u1 a2 u2 -- n }
  BEGIN u1 u2 AND 0> WHILE
    a1 C@ a2 C@ 2DUP = 0= IF
      2DUP < IF 2DROP -1 ELSE 2DROP 1 THEN EXIT
    THEN
    2DROP
    a1 1+ TO a1  u1 1- TO u1
    a2 1+ TO a2  u2 1- TO u2
  REPEAT
  u1 u2 < IF -1 ELSE u1 0> IF 1 ELSE 0 THEN THEN ;
: SEARCH    { a1 u1 a2 u2 -- ca n }
  u2 0= IF a1 u1 -1 EXIT THEN
  a1 TO ca  u1 TO n
  BEGIN n u2 >= WHILE
    ca u2 a2 u2 COMPARE 0= IF ca n -1 EXIT THEN
    ca 1+ TO ca  n 1- TO n
  REPEAT
  a1 u1 0 ;
: DIGIT?    { c -- n }  c 48 >= c 57 <= AND IF c 48 - ELSE -1 THEN ;
: >NUMBER   { lo hi a u -- }
  BEGIN u 0> WHILE
    a C@ DIGIT? DUP 0< IF DROP lo hi a u EXIT THEN
    lo 10 * +  DUP $FFFFFFFF AND
    SWAP 32 RSHIFT  hi 10 * +
    TO hi  TO lo
    a 1+ TO a  u 1- TO u
  REPEAT
  lo hi a u ;

: COUNT      ( a -- a+1 u )  DUP 1+ SWAP C@ ;
: /STRING    ( a u n -- a+n u-n )  DUP >R - SWAP R> + SWAP ;
: -TRAILING  ( a u -- a u' )
  BEGIN DUP 0> WHILE 2DUP 1- + C@ 32 = IF 1- ELSE EXIT THEN REPEAT ;
: CHAR       ( -- c )  PARSE-NAME DROP C@ ;
: [CHAR]     CHAR ['] (lit) COMPILE, CODE, ; IMMEDIATE
: BLANK      ( a u -- )  32 FILL ;
: SPACES     ( n -- )  BEGIN DUP 0> WHILE 32 EMIT 1- REPEAT DROP ;
: PLACE      { src u dst -- }  u dst C!  src dst 1+ u CMOVE ;

\ ---- number output (threaded Forth) --------------------------------------

8 CONSTANT CELL-SIZE
: CELLS  CELL-SIZE * ;
: VARIABLE CREATE 1 CELLS ALLOT ;
: +!     ( n addr -- ) DUP @ ROT + SWAP ! ;

CODE PICK
  require 1
  mov R0, [S1-1c]
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
  mov R0, [S1-1c]
  add R0, R0, #2
  shl R0, R0, #3
  mov R1, S1
  sub R1, R1, R0
  mov R2, [R1]
  mov [S1-1c], R2
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

\ ---- loop control --------------------------------------------------------

CODE UNLOOP
  mov R0, &rp
  mov.u32 R1, [R0]
  add R1, R1, #-2
  mov32 [R0], R1
;CODE

\ ---- tooling -------------------------------------------------------------

CODE NATIVE>
  require 1
  mov R0, [S1-1c]
  sig P P
  icall &forth_native
  mov [S1-1c], R0
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
