\ lib/objects.fs - instance object store with mark-and-sweep GC.
\ Load with `REQUIRE objects.fs`.
\
\ A store is an opaque handle to a record that owns all of the mutable GC
\ state (object list, live-byte counter, roots, address index, mark worklist)
\ and two arenas: one for objects (S-DATA) and one for the table/worklist
\ (S-META).  Every runtime word takes the store explicitly, so several
\ independent stores can coexist.  Object memory comes from the store's data
\ arena; freeing returns the chunk to the arena free list.
\
\ A type is a global, process-lifetime descriptor (shared metadata): with
\ NEWTYPE/FIELD/;TYPE you declare one, and its `kind` per field (scalar or
\ pointer) drives precise marking by default.  A type may instead supply a
\ custom TRACE `( obj store -- )`.
\
\ Roots are the data stack, the locals frame, the return stack, and any slot
\ registered with ROOT.  The store record offsets are cell offsets, so every
\ access adds CELLS to reach the byte address.

REQUIRE arena.fs

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
1 CONSTANT T-TRACE      \ ( obj store -- ) trace pointer fields, or 0
2 CONSTANT T-PRINT      \ ( obj -- ) or 0
3 CONSTANT T-LINK       \ catalog link
4 CONSTANT T-NFIELDS
5 CONSTANT T-FIELDS     \ ptr to field table
6 CONSTANT T-NAME       \ ptr to the copied type name
7 CONSTANT T-NAMELEN
8 CONSTANT T-HEADER

\ field entry (cells): [ name-addr, name-len, offset, kind ]
0 CONSTANT FT-NAME
1 CONSTANT FT-LEN
2 CONSTANT FT-OFF
3 CONSTANT FT-KIND
4 CONSTANT FT-SIZE

\ store record (cells)
0 CONSTANT S-OBJS        \ all-objects list head
1 CONSTANT S-USED        \ bytes currently allocated
2 CONSTANT S-LIMIT       \ GC threshold (bytes)
3 CONSTANT S-ROOTS       \ linked list of root cell addresses
4 CONSTANT S-TAB         \ address index base
5 CONSTANT S-TABCAP
6 CONSTANT S-TABN
7 CONSTANT S-TABTOMB
8 CONSTANT S-MKSTK       \ mark worklist
9 CONSTANT S-MKSP
10 CONSTANT S-MKCAP
11 CONSTANT S-DATA       \ object arena
12 CONSTANT S-CODE       \ executable (RWX) code arena
13 CONSTANT S-META       \ table/worklist arena
14 CONSTANT S-PARENT     \ ancestor store (0 for a root)
15 CONSTANT S-CATALOG    \ type descriptor list
16 CONSTANT S-CHILDREN   \ child-store list head (ownership, never GC-scanned)
17 CONSTANT S-SIBLING    \ next sibling in the parent's list
18 CONSTANT S-NCHILD     \ live child count (defer this store's GC while > 0)
19 CONSTANT S-MASK       \ low bits cleared when marking (0 = plain pointers)
20 CONSTANT S-BINDINGS   \ generic per-store object handle (e.g. a bindings table)
21 CONSTANT S-SYMBOLS    \ generic per-store object handle (e.g. symbol intern list)
22 CONSTANT S-SIZE

: CELL+ ( a -- a' ) CELL-SIZE + ;
: STR= { a1 u1 a2 u2 -- f } a1 u1 a2 u2 COMPARE 0= ;
: ALLOC-TEXT { a u -- addr } u ALLOC TO addr  a addr u MOVE  addr ;

\ store field access
: S+ ( store off -- addr ) CELLS + ;
: S@ ( store off -- x ) CELLS + @ ;
: S! ( x store off -- ) CELLS + ! ;

\ --------------------------------------------------------------------------
\ store lifecycle
\ STORE-MAKE ( parent limit -- store ): build a store, linking it into its
\ parent's child registry.  The registry is ownership metadata held in plain
\ (non-GC) records, so a parent->child edge is never scanned as a data
\ pointer: the "reference up only" rule stays intact.
: STORE-MAKE { parent limit -- store meta data }
  S-SIZE CELLS MALLOC TO store
  store 0= IF ARENA-OOM THROW THEN
  ARENA-OPEN TO meta
  ARENA-OPEN TO data
  limit 0 <= IF 1 20 LSHIFT TO limit THEN
  0     store S-OBJS S!
  0     store S-USED S!
  limit store S-LIMIT S!
  0     store S-ROOTS S!
  meta  store S-META S!
  data  store S-DATA S!
  data ARENA-VG-ON
  0 ARENA-NEW-CODE store S-CODE S!
  1024 CELLS meta ARENA-ALLOC store S-TAB S!
  store S-TAB S@ 1024 CELLS 0 FILL
  1024 store S-TABCAP S!
  0    store S-TABN S!
  0    store S-TABTOMB S!
  256 CELLS meta ARENA-ALLOC store S-MKSTK S!
  0    store S-MKSP S!
  256  store S-MKCAP S!
  parent  store S-PARENT S!
  0       store S-CATALOG S!
  0       store S-CHILDREN S!
  0       store S-SIBLING S!
  0       store S-NCHILD S!
  0       store S-MASK S!
  0       store S-BINDINGS S!
  0       store S-SYMBOLS S!
  parent IF
    STORE-LOCK
    parent S-CHILDREN S@ store S-SIBLING S!
    store  parent S-CHILDREN S!
    parent S-NCHILD S@ 1+ parent S-NCHILD S!
    STORE-UNLOCK
  THEN
  store ;

: STORE-NEW ( limit -- store ) 0 SWAP STORE-MAKE ;
: STORE-CHILD ( parent limit -- store ) STORE-MAKE ;

: STORE-UNLINK { store -- parent prev cur nextsib }
  store S-PARENT S@ TO parent
  parent 0= IF EXIT THEN
  STORE-LOCK
  0 TO prev
  parent S-CHILDREN S@ TO cur
  BEGIN cur WHILE
    cur store = IF
      cur S-SIBLING S@ TO nextsib
      prev IF nextsib prev S-SIBLING S! ELSE nextsib parent S-CHILDREN S! THEN
      parent S-NCHILD S@ 1- parent S-NCHILD S!
      0 TO cur                          \ remove: leave the loop
    ELSE
      cur TO prev
      cur S-SIBLING S@ TO cur
    THEN
  REPEAT
  STORE-UNLOCK ;

: STORE-FREE { store -- meta data code }
  store STORE-UNLINK
  store S-META S@ TO meta
  store S-DATA S@ TO data
  store S-CODE S@ TO code
  meta ARENA-DESTROY
  data ARENA-DESTROY
  code ARENA-DESTROY
  store FREE ;

\ --------------------------------------------------------------------------
\ address index: open addressing; slot 0 = empty, 1 = tombstone, else object
\ hot-path field accesses are inlined (S@ / S! are colon words)
: S-TAB-SLOT { store i -- addr } i CELLS store S-TAB CELLS + @ + ;
: S-TAB-HASH { store x -- i }
  x 3 RSHIFT
  2654435761 *
  DUP 32 RSHIFT XOR
  DUP 16 RSHIFT XOR
  store S-TABCAP CELLS + @ 1- AND ;

: S-TAB-PUT { store x -- i }
  store x S-TAB-HASH TO i
  BEGIN
    i CELLS store S-TAB CELLS + @ + @ 1 >
  WHILE
    i 1+ store S-TABCAP CELLS + @ 1- AND TO i
  REPEAT
  x i CELLS store S-TAB CELLS + @ + ! ;

: S-TAB-GROW { store -- oldtab oldcap newcap newtab }
  store S-TAB CELLS + @ TO oldtab
  store S-TABCAP CELLS + @ TO oldcap
  oldcap 2 * TO newcap
  newcap CELLS store S-META CELLS + @ ARENA-ALLOC TO newtab
  newtab newcap CELLS 0 FILL
  oldtab oldcap newtab newcap (TAB-REHASH)
  newtab store S-TAB CELLS + !
  newcap store S-TABCAP CELLS + !
  0 store S-TABTOMB CELLS + ! ;

: S-TAB-INSERT { store x -- }
  store S-TABN CELLS + @ store S-TABTOMB CELLS + @ + 4 * store S-TABCAP CELLS + @ 3 * >= IF store S-TAB-GROW THEN
  store x S-TAB-PUT
  1 store S-TABN CELLS + @ + store S-TABN CELLS + ! ;

: S-TAB-HAS? { store x -- i }
  store x S-TAB-HASH TO i
  BEGIN
    i CELLS store S-TAB CELLS + @ + @
    DUP 0= IF DROP 0 EXIT THEN
    DUP x = IF DROP -1 EXIT THEN
    DROP
    i 1+ store S-TABCAP CELLS + @ 1- AND TO i
  AGAIN ;

: S-TAB-REMOVE { store x -- i }
  store x S-TAB-HASH TO i
  BEGIN
    i CELLS store S-TAB CELLS + @ + @
    DUP 0= IF DROP EXIT THEN
    DUP x = IF
      DROP
      1 i CELLS store S-TAB CELLS + @ + !
      store S-TABN CELLS + @ 1- store S-TABN CELLS + !
      store S-TABTOMB CELLS + @ 1+ store S-TABTOMB CELLS + !
      EXIT
    THEN
    DROP
    i 1+ store S-TABCAP CELLS + @ 1- AND TO i
  AGAIN ;

\ --------------------------------------------------------------------------
\ mark worklist (explicit, so marking never recurses on the return stack)
: S-MK-GROW { store -- new capacity }
  store S-MKCAP S@ 2 * 4 A-MAX TO capacity
  capacity CELLS store S-META S@ ARENA-ALLOC TO new
  store S-MKSTK S@ new store S-MKSP S@ CELLS MOVE
  new store S-MKSTK S!
  capacity store S-MKCAP S! ;

: S-MK-PUSH { store x -- }
  store S-MKSP S@ store S-MKCAP S@ >= IF store S-MK-GROW THEN
  x store S-MKSTK S@ store S-MKSP S@ CELLS + !
  store S-MKSP S@ 1+ store S-MKSP S! ;

\ --------------------------------------------------------------------------
: FIELD-ADDR ( obj i -- addr ) CELLS SWAP H-PAYLOAD CELLS + + ;
: FIELD@ ( obj i -- x ) FIELD-ADDR @ ;
: FIELD! ( x obj i -- ) FIELD-ADDR ! ;
: TYPEOF ( obj -- type ) H-TYPE CELLS + @ ;

: S-MARK-CELL { store x -- m }
  x store S-MASK S@ INVERT AND TO m
  m 0= IF EXIT THEN
  store m S-TAB-HAS? IF
    m H-MARK CELLS + @ 0= IF
      m H-MARK CELLS + 1 SWAP !
      store m S-MK-PUSH
    THEN
  THEN ;

: S-MARK-OBJ { obj store -- type trace fields }
  obj H-TYPE CELLS + @ TO type
  type T-TRACE CELLS + @ TO trace
  trace IF obj store trace EXECUTE EXIT THEN
  type T-FIELDS CELLS + @ TO fields
  type T-NFIELDS CELLS + @ 0 ?DO
    fields I FT-SIZE * CELLS + FT-KIND CELLS + @ K-POINTER = IF
      store obj fields I FT-SIZE * CELLS + FT-OFF CELLS + @ FIELD-ADDR @ S-MARK-CELL
    THEN
  LOOP ;

\ --------------------------------------------------------------------------
\ roots
: S-MARK-SPAN { base n store -- } n 0 ?DO store base I CELLS + @ S-MARK-CELL LOOP ;
: S-MARK-RANGE { top n store -- } top CELLS + n CELLS - n store S-MARK-SPAN ;

VARIABLE RANGES

: S-MARK-ROOTS { store -- cur r }
  DSTACK store S-MARK-SPAN
  LOCALS store S-MARK-SPAN
  RSTACK store S-MARK-SPAN
  store S-ROOTS S@ TO cur
  BEGIN cur WHILE
    store cur @ @ S-MARK-CELL          \ node[0] = root cell address
    cur CELL-SIZE + @ TO cur
  REPEAT
  RANGES @ TO r
  BEGIN r WHILE
    r @ r CELL-SIZE + @ @ store S-MARK-SPAN    \ ( base count store )
    r 2 CELLS + @ TO r
  REPEAT ;

: S-ROOT { addr store -- node }
  2 CELLS ALLOC TO node
  addr node !
  store S-ROOTS S@ node CELL-SIZE + !
  node store S-ROOTS S! ;

\ register a cell range [base, base+count@) as roots (for evaluator stacks)
: S-ROOT-RANGE { base countaddr store -- node }
  3 CELLS ALLOC TO node
  base      node !
  countaddr node CELL-SIZE + !
  RANGES @  node 2 CELLS + !
  node RANGES ! ;

: S-UNROOT { addr store -- prev cur }
  0 TO prev
  store S-ROOTS S@ TO cur
  BEGIN cur WHILE
    cur @ addr = IF
      prev IF prev CELL-SIZE + cur CELL-SIZE + @ ! ELSE cur CELL-SIZE + @ store S-ROOTS S! THEN
      EXIT
    THEN
    cur TO prev
    cur CELL-SIZE + @ TO cur
  REPEAT ;

: S-DRAIN { store -- }
  BEGIN store S-MKSP S@ 0> WHILE
    store S-MKSP S@ 1- store S-MKSP S!
    store S-MKSTK S@ store S-MKSP S@ CELLS + @ store S-MARK-OBJ
  REPEAT ;

: S-FREEOBJ { obj store -- }
  store obj S-TAB-REMOVE
  obj H-SIZE CELLS + @ H-PAYLOAD + CELLS
  store S-USED S@ SWAP - store S-USED S!
  obj store S-DATA S@ ARENA-FREE ;

: S-SWEEP { store -- prev cur next }
  0 TO prev
  store S-OBJS S@ TO cur
  BEGIN cur WHILE
    cur H-NEXT CELLS + @ TO next
    cur H-MARK CELLS + @ 0= IF
      prev IF next prev H-NEXT CELLS + ! ELSE next store S-OBJS S! THEN
      cur store S-FREEOBJ
    ELSE
      0 cur H-MARK CELLS + !
      cur TO prev
    THEN
    next TO cur
  REPEAT ;

\ integrity check (live list/table + free list); enabled by DBG-VERIFY
VARIABLE DBG-VERIFY
: S-VERIFY { store -- n i o chunk arena }
  0 TO i
  store S-OBJS S@ TO o
  BEGIN o WHILE
    store o S-TAB-HAS? 0= IF -1 THROW THEN
    o H-MARK CELLS + @ 0 <> IF -1 THROW THEN
    i 1+ TO i
    i store S-TABN S@ > IF -1 THROW THEN
    o H-NEXT CELLS + @ TO o
  REPEAT
  i store S-TABN S@ <> IF -1 THROW THEN
  store S-DATA S@ TO arena
  arena A-FREE CELLS + @ TO chunk
  BEGIN chunk WHILE
    chunk 7 AND 0 <> IF -1 THROW THEN
    chunk @ 7 AND 0 <> IF -1 THROW THEN
    chunk @ 0 <= IF -1 THROW THEN
    chunk CELL-SIZE + @ TO chunk
  REPEAT
  i ;

\ A store with live children is kept all-live: its objects may be referenced
\ from any descendant, and there is no downward tracing, so collection is
\ deferred until the last child goes away.  Leaf stores collect normally.
: S-GC { store -- }
  store S-NCHILD S@ 0> IF EXIT THEN
  0 store S-MKSP S! store S-MARK-ROOTS store S-DRAIN store S-SWEEP
  DBG-VERIFY @ IF store S-VERIFY DROP THEN ;

\ --------------------------------------------------------------------------
\ allocation
\ S-NEW-N allocates an object with an explicit payload size (cells), so it
\ also backs variable-length arrays.  S-NEW is the fixed-size case.
: S-NEW-N { type ncells store -- obj }
  type ncells store (OBJ-FAST-N) ?DUP IF EXIT THEN
  store S-USED CELLS + @ store S-LIMIT CELLS + @ > IF store S-GC THEN
  ncells H-PAYLOAD + CELLS store S-DATA CELLS + @ ARENA-ALLOC TO obj
  obj 0= IF ARENA-OOM THROW THEN
  store S-OBJS CELLS + @ obj H-NEXT CELLS + !
  obj store S-OBJS CELLS + !
  type obj H-TYPE CELLS + !
  ncells obj H-SIZE CELLS + !
  0 obj H-MARK CELLS + !
  store obj S-TAB-INSERT
  ncells H-PAYLOAD + CELLS store S-USED CELLS + @ + store S-USED CELLS + !
  obj ;

: S-NEW { type store -- obj } type type T-SIZE CELLS + @ store S-NEW-N ;

\ --------------------------------------------------------------------------
\ type definition
\ NEWTYPE takes the target store; its descriptor is linked into that store's
\ catalog.  ;TYPE is idempotent: if a same-named type already exists in the
\ store or an ancestor, the freshly created word is aliased to it instead of
\ adding a duplicate descriptor.
VARIABLE NT-STORE
VARIABLE NT-WORD
VARIABLE NT-BODY
VARIABLE NT-TRACE
VARIABLE NT-PRINT
VARIABLE NT-NF
VARIABLE NT-FIELDS
VARIABLE NT-CAP
VARIABLE NT-NAME-ADDR
VARIABLE NT-NAME-LEN
VARIABLE F-KIND
VARIABLE F-ADDR
VARIABLE F-LEN
VARIABLE F-BASE

: NEWTYPE ( store "name" -- )
  NT-STORE !
  CREATE 0 ,
  LATEST NT-WORD !
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

: CATALOG-FIND { addr u cat -- type }
  BEGIN cat WHILE
    cat T-NAMELEN CELLS + @ u = IF
      cat T-NAME CELLS + @ cat T-NAMELEN CELLS + @ addr u STR= IF cat EXIT THEN
    THEN
    cat T-LINK CELLS + @ TO cat
  REPEAT 0 ;

: FIND-TYPE-CHAIN { addr u store -- type }
  BEGIN store WHILE
    addr u store S-CATALOG S@ CATALOG-FIND ?DUP IF EXIT THEN
    store S-PARENT S@ TO store
  REPEAT 0 ;

: ;TYPE { -- d }
  NT-WORD @ WORD-NAME NT-NAME-LEN ! NT-NAME-ADDR !
  NT-NAME-ADDR @ NT-NAME-LEN @ NT-STORE @ FIND-TYPE-CHAIN
  ?DUP IF NT-BODY @ ! EXIT THEN
  T-HEADER CELLS ALLOC TO d
  NT-NAME-ADDR @ NT-NAME-LEN @ ALLOC-TEXT d T-NAME CELLS + !
  NT-NAME-LEN @ d T-NAMELEN CELLS + !
  NT-NF @ d T-SIZE CELLS + !
  NT-TRACE @ d T-TRACE CELLS + !
  NT-PRINT @ d T-PRINT CELLS + !
  NT-NF @ d T-NFIELDS CELLS + !
  NT-NF @ FT-SIZE * CELLS ALLOC
  DUP d T-FIELDS CELLS + !
  NT-FIELDS @ SWAP  NT-NF @ FT-SIZE * CELLS  MOVE
  NT-STORE @ S-CATALOG S@ d T-LINK CELLS + !
  d NT-STORE @ S-CATALOG S!
  d NT-BODY @ ! ;

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
: OBJECTS-LIVE { store -- n } store S-TABN S@ ;

: OBJECTS-STATS { store -- }
  ." objects=" store S-TABN S@ .
  ." bytes=" store S-USED S@ .
  ." table=" store S-TABCAP S@ . CR ;

\ discard every object and reset the store to empty
: OBJECTS-FREE { store -- }
  store S-DATA S@ ARENA-RESET
  store S-CODE S@ ARENA-RESET
  0 store S-OBJS S!
  0 store S-TABN S!
  0 store S-TABTOMB S!
  0 store S-USED S!
  store S-TAB S@ store S-TABCAP S@ CELLS 0 FILL ;

\ integrity check (after GC): every live object is indexed, marks clear, the
\ list has no cycle and its length equals TABN.  Throws on any inconsistency.
: OBJECTS-VERIFY { store -- n i o }
  0 TO i
  store S-OBJS S@ TO o
  BEGIN o WHILE
    store o S-TAB-HAS? 0= IF -1 THROW THEN
    o H-MARK CELLS + @ 0 <> IF -1 THROW THEN
    i 1+ TO i
    i store S-TABN S@ > IF -1 THROW THEN
    o H-NEXT CELLS + @ TO o
  REPEAT
  i store S-TABN S@ <> IF -1 THROW THEN
  i ;

\ free-list integrity: every chunk is 8-aligned with a positive size.
: ARENA-VERIFY { arena -- n i chunk }
  0 TO i
  arena A-FREE CELLS + @ TO chunk
  BEGIN chunk WHILE
    chunk 7 AND 0 <> IF -1 THROW THEN
    chunk @ 0 <= IF -1 THROW THEN
    chunk @ 7 AND 0 <> IF -1 THROW THEN
    i 1+ TO i
    chunk CELL-SIZE + @ TO chunk
  REPEAT
  i ;

\ public names (store is the last argument)
: NEW ( type store -- obj ) S-NEW ;
: GC ( store -- ) S-GC ;
: ROOT ( addr store -- ) S-ROOT ;
: UNROOT ( addr store -- ) S-UNROOT ;
: ROOT-RANGE ( base countaddr store -- ) S-ROOT-RANGE ;
: STORE-PARENT ( store -- parent ) S-PARENT S@ ;
: STORE-NCHILDREN ( store -- n ) S-NCHILD S@ ;
: STORE-MASK ( store -- m ) S-MASK S@ ;
: STORE-MASK! ( m store -- ) S-MASK S! ;
: STORE-BINDINGS ( store -- x ) S-BINDINGS S@ ;
: STORE-BINDINGS! ( x store -- ) S-BINDINGS S! ;
: STORE-SYMBOLS ( store -- x ) S-SYMBOLS S@ ;
: STORE-SYMBOLS! ( x store -- ) S-SYMBOLS S! ;
: MARK ( store x -- ) S-MARK-CELL ;
: META-ALLOC ( u store -- addr ) S-META S@ ARENA-ALLOC ;

\ the system store: root of every store chain, created once at load time
VARIABLE SYS-STORE
1 20 LSHIFT STORE-NEW SYS-STORE !
: SYSTEM ( -- store ) SYS-STORE @ ;

\ --------------------------------------------------------------------------
\ per-task stores
\ Each spawned task attaches its own child of SYSTEM (shared-nothing mutable
\ heap) and releases it when the body finishes.  TASK-STORE is the store to
\ use from within a task; the root context's store is SYSTEM itself.
: (TASK-ENTER)  SYSTEM 0 STORE-CHILD (SET-STORE) ;
: (TASK-LEAVE)  (GET-STORE) ?DUP IF STORE-FREE THEN  0 (SET-STORE) ;
: TASK-STORE ( -- store ) (GET-STORE) ;

' (TASK-ENTER) ' (TASK-LEAVE) (TASK-HOOKS)
SYSTEM (SET-STORE)

\ --------------------------------------------------------------------------
\ code objects
\ A CODE-OBJ holds an executable code blob copied into the store's RWX code
\ arena; it is freed with the store.  NEW-CODE copies `from`/`u` and returns
\ the object; the source (e.g. a sljit block) can then be released.  The
\ `addr` field is a scalar (not a heap pointer), so GC never traces it.
SYSTEM NEWTYPE CODE-OBJ
  0 FIELD addr
  0 FIELD size
;TYPE

: NEW-CODE { from u store -- codeobj dst }
  u store S-CODE S@ ARENA-ALLOC TO dst
  from dst u MOVE
  CODE-OBJ store NEW TO codeobj
  dst codeobj 0 FIELD!
  u   codeobj 1 FIELD!
  codeobj ;

: CODE-PTR { codeobj -- addr } codeobj 0 FIELD@ ;
: CODE-SIZE { codeobj -- u } codeobj 1 FIELD@ ;

\ --------------------------------------------------------------------------
\ arrays: variable-length, GC-traced base elements of the store.
\ An array's payload is H-SIZE generic cells.  Marking is conservative: every
\ cell is scanned and only values that are objects of this store are marked,
\ so numbers / null are ignored.  That never misses a live reference (sound);
\ at worst a number aliasing an object address retains that object.
: TRACE-ALL { obj store -- n i }
  obj H-SIZE CELLS + @ TO n
  n 0 ?DO store obj H-PAYLOAD CELLS + I CELLS + @ S-MARK-CELL LOOP ;

SYSTEM NEWTYPE ARRAY
  TRACE: TRACE-ALL
;TYPE

: ARRAY-NEW { ncells store -- arr }
  ARRAY ncells store S-NEW-N TO arr
  arr H-PAYLOAD CELLS + ncells CELLS 0 FILL
  arr ;
: ARRAY-LEN { arr -- n } arr H-SIZE CELLS + @ ;
: ARRAY@ { arr i -- x } arr H-PAYLOAD CELLS + i CELLS + @ ;
: ARRAY! { x arr i -- } x arr H-PAYLOAD CELLS + i CELLS + ! ;
