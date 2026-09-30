\ ---- generic bytecode VM (skeleton) --------------------------------------
\ A tiny stack machine with explicit state, dispatched through EXECUTE.
\ Language front-ends compile to this instruction stream.  Keeping the VM
\ state explicit (pc, own operand stack) is what later enables call/cc and
\ coroutines: a continuation is a captured copy of this state.

0 CONSTANT OP-HALT
1 CONSTANT OP-PUSH
2 CONSTANT OP-ADD
3 CONSTANT OP-SUB
4 CONSTANT OP-MUL
5 CONSTANT OP-DUP
6 CONSTANT OP-DROP
7 CONSTANT OP-JMP
8 CONSTANT OP-JZ
9 CONSTANT OP-PRINT
10 CONSTANT OP-SWAP
11 CONSTANT OP-OVER
12 CONSTANT OP-CC
13 CONSTANT OP-INVOKE

VARIABLE VM-PC
VARIABLE VM-SP
VARIABLE VM-CODE
VARIABLE VM-STK
4096 CELLS ALLOC VM-STK !

: VM-RESET ( -- ) 0 VM-SP ! ;
: VM-BIND ( code -- ) VM-CODE ! VM-RESET ;
: VTOP ( -- addr ) VM-STK @ VM-SP @ 1- CELLS + ;
: VPUSH ( x -- ) VM-STK @ VM-SP @ CELLS + !  1 VM-SP +! ;
: VPOP ( -- x ) VTOP @  -1 VM-SP +! ;
: VM-NEXT ( -- ) 1 VM-PC +! ;
: VM-ARG ( -- x ) VM-PC @ 1+ CELLS VM-CODE @ + @ ;

: OP-HALT* ( -- ) -1 VM-PC ! ;
: OP-PUSH* ( -- ) VM-ARG VPUSH  2 VM-PC +! ;
: OP-ADD* ( -- ) VPOP VPOP + VPUSH VM-NEXT ;
: OP-SUB* ( -- ) VPOP VPOP SWAP - VPUSH VM-NEXT ;
: OP-MUL* ( -- ) VPOP VPOP * VPUSH VM-NEXT ;
: OP-DUP* ( -- ) VPOP DUP VPUSH VPUSH VM-NEXT ;
: OP-DROP* ( -- ) VPOP DROP VM-NEXT ;
: OP-JMP* ( -- ) VM-ARG VM-PC ! ;
: OP-JZ* ( -- ) VPOP 0= IF VM-ARG VM-PC ! ELSE 2 VM-PC +! THEN ;
: OP-PRINT* ( -- ) VPOP . VM-NEXT ;
: OP-SWAP* ( -- ) VPOP VPOP SWAP VPUSH VPUSH VM-NEXT ;
: OP-OVER* ( -- ) VPOP VPOP DUP VPUSH SWAP VPUSH VPUSH VM-NEXT ;

\ ---- continuations: capture / restore the explicit VM state ------------

: VM-COPY { src dst n -- } n 0 ?DO src I CELLS + @ dst I CELLS + ! LOOP ;

\ capture a continuation resuming at `pc` with the current operand stack
: VM-CAPTURE { pc -- k }
  VM-SP @ 2 + CELLS ALLOC TO k
  pc k !
  VM-SP @ k 1 CELLS + !
  VM-STK @ k 2 CELLS + VM-SP @ VM-COPY
  k ;

\ restore continuation k and deliver value v as the call/cc result
: VM-INSTALL { k v -- }
  k @ VM-PC !
  k 1 CELLS + @ VM-SP !
  k 2 CELLS + VM-STK @ VM-SP @ VM-COPY
  v VPUSH ;

: OP-CC* ( -- ) VM-ARG VM-CAPTURE VPUSH  2 VM-PC +! ;
: OP-INVOKE* ( -- ) VPOP VPOP SWAP VM-INSTALL ;

CREATE VM-OPS
' OP-HALT*  ,
' OP-PUSH*  ,
' OP-ADD*   ,
' OP-SUB*   ,
' OP-MUL*   ,
' OP-DUP*   ,
' OP-DROP*  ,
' OP-JMP*   ,
' OP-JZ*    ,
' OP-PRINT* ,
' OP-SWAP*  ,
' OP-OVER*  ,
' OP-CC*    ,
' OP-INVOKE* ,

: VM-STEP ( -- ) VM-PC @ CELLS VM-CODE @ + @ CELLS VM-OPS + @ EXECUTE ;
: VM-RUN ( -- ) BEGIN VM-STEP VM-PC @ 0< UNTIL ;

\ ---- coroutines: independent VM states (stack + code + pc + sp) --------
\ A state header is [ stack-base, code, pc, sp ].

VARIABLE VM-CUR

: VM-STATE-NEW { code -- co }
  4 CELLS ALLOC TO co
  4096 CELLS ALLOC co !
  code co 1 CELLS + !
  0 co 2 CELLS + !
  0 co 3 CELLS + !
  co ;

: VM-SWITCH { co -- }
  VM-CUR @ IF
    VM-STK @  VM-CUR @ !
    VM-CODE @ VM-CUR @ 1 CELLS + !
    VM-PC @   VM-CUR @ 2 CELLS + !
    VM-SP @   VM-CUR @ 3 CELLS + !
  THEN
  co VM-CUR !
  co @              VM-STK !
  co 1 CELLS + @    VM-CODE !
  co 2 CELLS + @    VM-PC !
  co 3 CELLS + @    VM-SP ! ;
