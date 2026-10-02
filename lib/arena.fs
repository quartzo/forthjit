\ lib/arena.fs - instance arenas backed by anonymous mmap.
\ Load with `REQUIRE arena.fs`.
\
\ Allocation policy lives here, in Forth; C only supplies MAP-RW / MAP-RWX /
\ UNMAP (forth.c).  An arena grows in blocks (default 64 KiB) and recycles
\ freed chunks through a first-fit free list.  There is no coalescing, so
\ fragmentation is bounded only by the allocation pattern.  An arena is an
\ opaque handle passed explicitly, so any number of independent arenas (data,
\ code, ...) can coexist.
\
\ layout
\   block:  [ next | cap | used | payload... ]        (B-HDR cells of header)
\   chunk:  [ size | next-free | payload... ]         (C-HDR cells of header)
\ `size` is the whole chunk in bytes, header included and 8-byte aligned.
\ The A-* constants are cell offsets into the arena record, so every access
\ adds CELLS to reach the byte address.

65536 CONSTANT ARENA-BLOCK      \ default block size (bytes)
-20   CONSTANT ARENA-OOM        \ out of memory

\ block header (cell offsets from the block base)
0 CONSTANT B-NEXT
1 CONSTANT B-CAP
2 CONSTANT B-USED
3 CONSTANT B-HDR

\ chunk header (cell offsets from the chunk base)
0 CONSTANT C-SIZE
1 CONSTANT C-HDR

\ arena record (cell offsets from the handle)
0 CONSTANT A-BLOCK
1 CONSTANT A-HEAD
2 CONSTANT A-CUR
3 CONSTANT A-FREE
4 CONSTANT A-EXEC
5 CONSTANT A-VG          \ 1 = poison free chunks (Valgrind debugging)
6 CONSTANT A-SIZE

: A-MAX ( a b -- max ) 2DUP < IF SWAP THEN DROP ;
: ALIGN8 ( n -- n' ) 7 + -8 AND ;

: ARENA-NEW { block -- arena }
  A-SIZE CELLS MALLOC TO arena
  arena 0= IF ARENA-OOM THROW THEN
  block 0 <= IF ARENA-BLOCK TO block THEN
  block arena A-BLOCK CELLS + !
  0     arena A-HEAD  CELLS + !
  0     arena A-CUR   CELLS + !
  0     arena A-FREE  CELLS + !
  0     arena A-EXEC  CELLS + !
  0     arena A-VG    CELLS + !
  arena ;

: ARENA-VG-ON ( arena -- ) -1 SWAP A-VG CELLS + ! ;

: ARENA-OPEN ( -- arena ) ARENA-BLOCK ARENA-NEW ;

: ARENA-NEW-CODE { block -- arena }
  block ARENA-NEW TO arena
  1 arena A-EXEC CELLS + !
  arena ;

: ARENA-ALLOC { n arena -- addr prev c rest b grow bcap sz nx }
  n ALIGN8 C-HDR CELLS + TO n
  \ first-fit over the free list
  0 TO prev
  arena A-FREE CELLS + @ TO c
  BEGIN c WHILE
    arena A-VG CELLS + @ IF c (VG-OPEN-HDR) ELSE c @ THEN TO sz
    c CELL-SIZE + @ TO nx
    arena A-VG CELLS + @ IF c (VG-CLOSE-HDR) THEN
    sz n >= IF
      arena A-VG CELLS + @ IF c sz (VG-OPEN-ALL) THEN   \ whole chunk becomes live
      sz n - 2 CELLS >= IF          \ a free chunk holds size + next (2 cells)
        c n + TO rest
        sz n - rest !
        nx rest CELL-SIZE + !
        prev IF
          arena A-VG CELLS + @ IF prev (VG-OPEN-HDR) DROP THEN
          rest prev CELL-SIZE + !
          arena A-VG CELLS + @ IF prev (VG-CLOSE-HDR) THEN
        ELSE
          rest arena A-FREE CELLS + !
        THEN
        n c !                        \ the allocated chunk is exactly n bytes
        arena A-VG CELLS + @ IF rest rest @ (VG-CLOSE-ALL) THEN
      ELSE
        prev IF
          arena A-VG CELLS + @ IF prev (VG-OPEN-HDR) DROP THEN
          nx prev CELL-SIZE + !
          arena A-VG CELLS + @ IF prev (VG-CLOSE-HDR) THEN
        ELSE
          nx arena A-FREE CELLS + !
        THEN
      THEN
      c C-HDR CELLS + EXIT
    THEN
    c TO prev
    nx TO c
  REPEAT
  \ bump allocation inside the current block
  arena A-CUR CELLS + @ TO b
  b 0= IF 1 TO grow
  ELSE b B-USED CELLS + @ n + b B-CAP CELLS + @ B-HDR CELLS - > TO grow THEN
  grow IF
    arena A-BLOCK CELLS + @ n A-MAX TO bcap
    bcap arena A-EXEC CELLS + @ IF MAP-RWX ELSE MAP-RW THEN TO b
    b 0= IF ARENA-OOM THROW THEN
    arena A-HEAD CELLS + @ b B-NEXT CELLS + !
    bcap b B-CAP CELLS + !
    0 b B-USED CELLS + !
    b arena A-HEAD CELLS + !
    b arena A-CUR CELLS + !
  THEN
  b B-HDR CELLS + C-HDR CELLS + b B-USED CELLS + @ + TO addr
  n addr C-HDR CELLS - !
  b B-USED CELLS + @ n + b B-USED CELLS + !
  addr ;

: ARENA-FREE { addr arena -- chunk }
  addr 0= IF EXIT THEN
  addr C-HDR CELLS - TO chunk
  arena A-FREE CELLS + @ chunk CELL-SIZE + !
  chunk arena A-FREE CELLS + !
  arena A-VG CELLS + @ IF chunk chunk @ (VG-CLOSE-ALL) THEN ;

: ARENA-RESET { arena -- b }
  arena A-HEAD CELLS + @ TO b
  BEGIN b WHILE
    arena A-VG CELLS + @ IF b B-HDR CELLS + b B-CAP CELLS + @ B-HDR CELLS - (VG-OPEN-ALL) THEN
    0 b B-USED CELLS + !
    b B-NEXT CELLS + @ TO b
  REPEAT
  arena A-HEAD CELLS + @ arena A-CUR CELLS + !
  0 arena A-FREE CELLS + ! ;

: ARENA-DESTROY { arena -- b next }
  arena A-HEAD CELLS + @ TO b
  BEGIN b WHILE
    b B-NEXT CELLS + @ TO next
    b b B-CAP CELLS + @ UNMAP
    next TO b
  REPEAT
  arena FREE ;

: ARENA-BLOCKS { arena -- n b }
  0 TO n
  arena A-HEAD CELLS + @ TO b
  BEGIN b WHILE 1 n + TO n  b B-NEXT CELLS + @ TO b REPEAT
  n ;
