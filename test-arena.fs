\ lib/arena.fs smoke tests
REQUIRE arena.fs

: CHECK { f a u -- }
  f 0= IF S" FAIL: " TYPE a u TYPE CR -1 THROW THEN ;

VARIABLE ARW
VARIABLE AA
VARIABLE AB
VARIABLE AC
VARIABLE CARN
VARIABLE CP

ARENA-OPEN ARW !
ARW @ 0 <> S" fresh arena" CHECK

\ allocate, write, read back
64 ARW @ ARENA-ALLOC AA !
AA @ 0 <> S" alloc nonzero" CHECK
12345 AA @ !
AA @ @ 12345 = S" write/read" CHECK

\ free then re-allocate the same size reuses the chunk (first-fit)
AA @ ARW @ ARENA-FREE
64 ARW @ ARENA-ALLOC AB !
AB @ AA @ = S" first-fit reuse" CHECK

\ a bigger-than-block allocation forces a second block
300000 ARW @ ARENA-ALLOC AC !
AC @ 0 <> S" big alloc" CHECK
777 AC @ !
AC @ @ 777 = S" big write/read" CHECK
ARW @ ARENA-BLOCKS 2 = S" two blocks" CHECK

\ the free list reclaims big chunks (with splitting)
AC @ ARW @ ARENA-FREE
100000 ARW @ ARENA-ALLOC AC !
AC @ 0 <> S" reuse big" CHECK

\ reset discards chunks but keeps blocks
ARW @ ARENA-RESET
ARW @ ARENA-BLOCKS 2 = S" blocks after reset" CHECK
32 ARW @ ARENA-ALLOC AA !
AA @ 0 <> S" alloc after reset" CHECK

\ a code arena maps RWX and allocates the same way
1024 ARENA-NEW-CODE CARN !
1024 CARN @ ARENA-ALLOC CP !
CP @ 0 <> S" code arena alloc" CHECK
42 CP @ !
CP @ @ 42 = S" code write/read" CHECK
CARN @ ARENA-DESTROY

\ an explicit block size is honored
8 ARENA-NEW CARN !
CARN @ 0 <> S" explicit block" CHECK
8 CARN @ ARENA-ALLOC CP !
CP @ 0 <> S" tiny block alloc" CHECK
CARN @ ARENA-DESTROY

ARW @ ARENA-DESTROY

S" arena tests passed" TYPE CR
