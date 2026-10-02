\ divide-and-conquer parallel sum benchmark
\ usage: time ./forth bench-par.fs   (FORTH_WORKERS controls the pool size)

: SEQ { lo hi -- s }        \ sequential sum of [lo,hi)
  0  hi lo  ?DO I + LOOP ;

: PS { lo hi -- s }         \ parallel sum with a sequential cutoff
  hi lo - 65536 < IF lo hi SEQ EXIT THEN
  lo hi + 2 /               ( mid )
  >R
  lo R@ ['] PS 2 SPAWN
  R> hi ['] PS 2 SPAWN
  >R JOIN R> JOIN + ;

: BENCH ( n -- )
  0 SWAP PS
  S" sum = " TYPE . CR ;

40000000 BENCH
