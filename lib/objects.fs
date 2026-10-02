\ lib/objects.fs - dynamic object store with mark-and-sweep GC.
\ Load with `REQUIRE objects.fs`.
\
\ Objects are malloc'd individually (stable, non-moving addresses) and linked
\ into one list.  A type is a Forth-defined descriptor with positional
\ callback slots and a field table; each field declares its `kind` (scalar or
\ pointer), so marking is precise by default.  A type may instead supply a
\ custom TRACE callback.  Roots are the data stack, the locals frame, the
\ return stack, and any slot registered with ROOT.

\ --------------------------------------------------------------------------
\ field kinds
0 CONSTANT K-SCALAR
1 CONSTANT K-POINTER

\ object header (cells)
0 CONSTANT H-NEXT       \ all-objects list link
1 CONSTANT H-TYPE
2 CONSTANT H-SIZE       \ payload cells
3 CONSTANT H-MARK
4 CONSTANT H-PAYLOAD

\ type descriptor (cells)
0 CONSTANT T-SIZE       \ payload cells (defaults to field count)
1 CONSTANT T-TRACE      \ ( obj -- ) trace pointer fields, or 0
2 CONSTANT T-PRINT      \ ( obj -- ) or 0
3 CONSTANT T-LINK       \ catalog link
4 CONSTANT T-NFIELDS
5 CONSTANT T-FIELDS     \ ptr to field table
6 CONSTANT T-HEADER

\ field entry (cells): [ name-addr, name-len, offset, kind ]
0 CONSTANT FT-NAME
1 CONSTANT FT-LEN
2 CONSTANT FT-OFF
3 CONSTANT FT-KIND
4 CONSTANT FT-SIZE

: CELL+ ( a -- a' ) CELL-SIZE + ;
: STR= { a1 u1 a2 u2 -- f } a1 u1 a2 u2 COMPARE 0= ;
: ALLOC-TEXT { a u -- addr } u ALLOC TO addr  a addr u MOVE  addr ;

\ --------------------------------------------------------------------------
\ store state
VARIABLE OBJS       \ all-objects list head
VARIABLE USED       \ bytes currently allocated on the GC heap
VARIABLE LIMIT       \ GC threshold (bytes)
VARIABLE CATALOG       \ type descriptor list
VARIABLE ROOTS       \ linked list of root cell addresses

\ address index: open addressing; slot 0 = empty, 1 = tombstone, else object
VARIABLE TAB
VARIABLE TAB-CAP
VARIABLE TAB-N
VARIABLE TAB-TOMB
VARIABLE OTAB
VARIABLE OC
VARIABLE XQ

: TAB-SLOT ( i -- addr ) CELLS TAB @ + ;
: TAB-HASH ( x -- i ) 3 RSHIFT 2654435761 * TAB-CAP @ 1- AND ;

: TAB-PUT ( x -- )
  DUP TAB-HASH
  BEGIN DUP TAB-SLOT @ 1 > WHILE 1+ TAB-CAP @ 1- AND REPEAT
  TAB-SLOT ! ;

VARIABLE NEWCAP
: TAB-GROW
  TAB @ OTAB !  TAB-CAP @ OC !
  OC @ 2 * DUP TAB-CAP ! NEWCAP !
  NEWCAP @ CELLS ALLOC TAB !
  TAB @ NEWCAP @ CELLS 0 FILL
  0 TAB-TOMB !
  OC @ 0 ?DO OTAB @ I CELLS + @ DUP 1 > IF TAB-PUT ELSE DROP THEN LOOP ;

: TAB-INSERT ( x -- )
  TAB-N @ TAB-TOMB @ + 4 * TAB-CAP @ 3 * >= IF TAB-GROW THEN
  TAB-PUT  1 TAB-N +! ;

: TAB-HAS? ( x -- f )
  DUP XQ ! TAB-HASH
  BEGIN
    DUP TAB-SLOT @
    DUP 0= IF 2DROP 0 EXIT THEN
    DUP XQ @ = IF 2DROP -1 EXIT THEN
    DROP 1+ TAB-CAP @ 1- AND
  AGAIN ;

: TAB-REMOVE ( x -- )
  XQ ! XQ @ TAB-HASH
  BEGIN
    DUP TAB-SLOT @
    DUP 0= IF 2DROP EXIT THEN
    DUP XQ @ = IF
      DROP 1 SWAP TAB-SLOT !
      TAB-N @ 1- TAB-N !  TAB-TOMB @ 1+ TAB-TOMB !  EXIT
    THEN
    DROP 1+ TAB-CAP @ 1- AND
  AGAIN ;

: TAB-INIT 1024 CELLS ALLOC TAB ! 1024 TAB-CAP ! 0 TAB-N ! 0 TAB-TOMB ! ;

\ --------------------------------------------------------------------------
\ mark worklist (explicit, so marking never recurses on the return stack)
VARIABLE MK-STK
VARIABLE MK-SP
VARIABLE MK-CAP
VARIABLE MNEW

: MK-GROW
  MK-CAP @ 2 * 4 MAX DUP MK-CAP !
  CELLS ALLOC MNEW !
  MK-STK @ MNEW @ MK-SP @ CELLS MOVE
  MNEW @ MK-STK ! ;
: MK-PUSH ( x -- )
  MK-SP @ MK-CAP @ >= IF MK-GROW THEN
  MK-STK @ MK-SP @ CELLS + !  1 MK-SP +! ;
: MK-INIT  256 CELLS ALLOC MK-STK ! 0 MK-SP ! 256 MK-CAP ! ;

\ --------------------------------------------------------------------------
: FIELD-ADDR ( obj i -- addr ) CELLS SWAP H-PAYLOAD CELLS + + ;
: FIELD@ ( obj i -- x ) FIELD-ADDR @ ;
: FIELD! ( x obj i -- ) FIELD-ADDR ! ;
: TYPEOF ( obj -- type ) H-TYPE CELLS + @ ;

: MARK-CELL ( x -- )
  ?DUP IF
    DUP TAB-HAS? IF
      DUP H-MARK CELLS + @ 0= IF
        DUP H-MARK CELLS + 1 SWAP !
        MK-PUSH
      ELSE DROP THEN
    ELSE DROP THEN
  THEN ;

VARIABLE MO
VARIABLE MF
VARIABLE MT
: MARK-OBJ ( obj -- )
  DUP H-TYPE CELLS + @ MT !
  MT @ T-TRACE CELLS + @ ?DUP IF EXECUTE ELSE
    MO !
    MT @ T-FIELDS CELLS + @ MF !
    MT @ T-NFIELDS CELLS + @ 0 ?DO
      MF @ I FT-SIZE * CELLS + FT-KIND CELLS + @ K-POINTER = IF
        MO @ MF @ I FT-SIZE * CELLS + FT-OFF CELLS + @ FIELD-ADDR @ MARK-CELL
      THEN
    LOOP
  THEN ;

\ --------------------------------------------------------------------------
\ roots
: MARK-SPAN { base n -- } n 0 ?DO base I CELLS + @ MARK-CELL LOOP ;
: MARK-RANGE { top n -- } top CELLS + n CELLS - n MARK-SPAN ;

: MARK-ROOTS
  DSTACK MARK-SPAN              \ data stack
  LOCALS MARK-SPAN              \ live locals frame
  RSTACK MARK-SPAN              \ return stack
  ROOTS @
  BEGIN DUP WHILE
    DUP @ @ MARK-CELL          \ value held in the registered slot
    1 CELLS + @
  REPEAT DROP ;

: ROOT ( addr -- )              \ register a global cell as a root
  2 CELLS ALLOC
  OVER OVER !
  ROOTS @ OVER 1 CELLS + !
  ROOTS ! ;

: UNROOT { addr -- prev cur }
  0 TO prev
  ROOTS @ TO cur
  BEGIN cur WHILE
    cur @ addr = IF
      prev IF prev 1 CELLS + cur 1 CELLS + @ ! ELSE cur 1 CELLS + @ ROOTS ! THEN
      EXIT
    THEN
    cur TO prev
    cur 1 CELLS + @ TO cur
  REPEAT ;

: DRAIN
  BEGIN MK-SP @ 0> WHILE
    MK-SP @ 1- MK-SP !
    MK-STK @ MK-SP @ CELLS + @ MARK-OBJ
  REPEAT ;

: FREEOBJ ( obj -- )
  DUP TAB-REMOVE
  DUP H-SIZE CELLS + @ H-PAYLOAD + CELLS
  USED @ SWAP - USED !
  FREE ;

: SWEEP { -- prev cur next }
  0 TO prev
  OBJS @ TO cur
  BEGIN cur WHILE
    cur H-NEXT CELLS + @ TO next
    cur H-MARK CELLS + @ 0= IF
      prev IF prev H-NEXT CELLS + next ! ELSE next OBJS ! THEN
      cur FREEOBJ
    ELSE
      0 cur H-MARK CELLS + !
      cur TO prev
    THEN
    next TO cur
  REPEAT ;

: GC  0 MK-SP ! MARK-ROOTS DRAIN SWEEP ;

\ --------------------------------------------------------------------------
\ allocation
: NEW { type -- obj }
  USED @ LIMIT @ > IF GC THEN
  type T-SIZE CELLS + @ H-PAYLOAD + CELLS MALLOC TO obj
  obj 0= IF -1 THROW THEN
  OBJS @ obj H-NEXT CELLS + !
  obj OBJS !
  type obj H-TYPE CELLS + !
  type T-SIZE CELLS + @ obj H-SIZE CELLS + !
  0 obj H-MARK CELLS + !
  obj TAB-INSERT
  H-PAYLOAD type T-SIZE CELLS + @ + CELLS USED +!
  obj ;

\ --------------------------------------------------------------------------
\ type definition
VARIABLE NT-BODY
VARIABLE NT-TRACE
VARIABLE NT-PRINT
VARIABLE NT-NF
VARIABLE NT-FIELDS
VARIABLE NT-CAP
VARIABLE F-KIND
VARIABLE F-ADDR
VARIABLE F-LEN
VARIABLE F-BASE

: NEWTYPE ( "name" -- )
  CREATE 0 ,
  LATEST >BODY NT-BODY !
  0 NT-TRACE ! 0 NT-PRINT !
  0 NT-NF !
  64 FT-SIZE * CELLS ALLOC NT-FIELDS !
  64 NT-CAP !
  DOES> @ ;

: FIELD ( kind "name" -- )
  F-KIND !
  PARSE-NAME F-LEN ! F-ADDR !
  NT-NF @ NT-CAP @ >= IF -1 THROW THEN
  NT-FIELDS @ NT-NF @ FT-SIZE * CELLS + F-BASE !
  F-ADDR @ F-LEN @ ALLOC-TEXT  F-BASE @ FT-NAME CELLS + !
  F-LEN @   F-BASE @ FT-LEN CELLS + !
  NT-NF @  F-BASE @ FT-OFF CELLS + !
  F-KIND @ F-BASE @ FT-KIND CELLS + !
  NT-NF @ 1+ NT-NF ! ;

: TRACE: ( "name" -- ) ' NT-TRACE ! ;
: PRINT: ( "name" -- ) ' NT-PRINT ! ;

: ;TYPE
  T-HEADER CELLS ALLOC >R
  NT-NF @ R@ T-SIZE CELLS + !
  NT-TRACE @ R@ T-TRACE CELLS + !
  NT-PRINT @ R@ T-PRINT CELLS + !
  NT-NF @ R@ T-NFIELDS CELLS + !
  NT-NF @ FT-SIZE * CELLS ALLOC
  DUP R@ T-FIELDS CELLS + !
  NT-FIELDS @ SWAP  NT-NF @ FT-SIZE * CELLS  MOVE
  CATALOG @ R@ T-LINK CELLS + !
  R@ CATALOG !
  R> NT-BODY @ ! ;

\ named field lookup / access
VARIABLE FE
: FIND-FIELD { type a u -- i }
  type T-FIELDS CELLS + @
  type T-NFIELDS CELLS + @ 0 ?DO
    DUP I FT-SIZE * CELLS + FE !
    FE @ FT-LEN CELLS + @ u = IF
      FE @ FT-NAME CELLS + @  FE @ FT-LEN CELLS + @  a u STR= IF
        DROP I UNLOOP EXIT
      THEN
    THEN
  LOOP DROP -1 ;

: @FIELD { obj a u -- x }
  obj H-TYPE CELLS + @ a u FIND-FIELD
  obj SWAP FIELD@ ;
: !FIELD { x obj a u -- }
  obj H-TYPE CELLS + @ a u FIND-FIELD   ( i )
  x SWAP obj SWAP FIELD! ;

\ --------------------------------------------------------------------------
: OBJECTS-INIT
  TAB-INIT MK-INIT
  LIMIT @ 0= IF 1 20 LSHIFT LIMIT ! THEN ;

: OBJECTS-STATS
  ." objects=" TAB-N @ .
  ." bytes=" USED @ .
  ." table=" TAB-CAP @ . CR ;

\ free every live object (call before exit, or leave to libc at process end)
: OBJECTS-FREE
  OBJS @ BEGIN DUP WHILE DUP H-NEXT CELLS + @ SWAP FREE REPEAT DROP
  0 OBJS !  0 TAB-N !  0 TAB-TOMB !  0 USED ! ;

OBJECTS-INIT
