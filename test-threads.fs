\ work-stealing task scheduler smoke tests

S" workers: " TYPE N-WORKERS . CR

: TRIPLE ( n -- 3n ) 3 * ;
7 ' TRIPLE 1 SPAWN JOIN . CR            \ 21

\ parallel fibonacci (spawns both branches)
: FIB ( n -- m )
  DUP 2 < IF EXIT THEN
  DUP 1- ['] FIB 1 SPAWN
  OVER 2 - ['] FIB 1 SPAWN
  >R JOIN R> JOIN
  >R SWAP DROP R> + ;

S" fib(10) = " TYPE 10 FIB . CR         \ 55
S" fib(15) = " TYPE 15 FIB . CR         \ 610

\ many independent jobs
: WORK ( n -- n*n ) DUP * ;
: SUMJOBS ( -- s )
  0 10 0 DO I ['] WORK 1 SPAWN JOIN + LOOP ;
S" sum 0..9 squares = " TYPE SUMJOBS . CR   \ 285

\ atomics: 10 tasks x 1000 atomic increments
VARIABLE CNT
: BUMP ( n -- ) 0 ?DO 1 CNT @+! LOOP ;
: RUN-BUMPS ( -- ) 10 0 DO 1000 ['] BUMP 1 SPAWN LOOP  10 0 DO JOIN LOOP ;
0 CNT A! RUN-BUMPS
S" atomic counter = " TYPE CNT @ . CR      \ 10000

\ atomic compare-and-swap spinlock
VARIABLE LK
: LOCK   ( -- ) BEGIN 0 1 LK CAS 0= UNTIL ;
: UNLOCK ( -- ) 0 LK A! ;
0 LK A! LOCK UNLOCK
S" lock value = " TYPE LK @ . CR          \ 0

\ PAR: rayon-style fork/join of two unary XTs
: SQ ( n -- n*n ) DUP * ;
S" par = " TYPE 5 ' SQ 6 ' SQ PAR . . CR   \ 36 25

\ channels: producer/consumer, buffered and rendezvous
: PRODUCE { ch -- } 100 0 DO I ch >CHAN LOOP ;
: CONSUME { ch -- s } 0 100 0 DO ch CHAN> + LOOP ;
: CHAN-TEST ( cap -- s )
  CHAN
  DUP ['] PRODUCE 1 SPAWN
  >R
  DUP ['] CONSUME 1 SPAWN
  R> JOIN JOIN NIP ;
S" chan sum = " TYPE 4 CHAN-TEST . CR       \ 4950
S" rendezvous sum = " TYPE 0 CHAN-TEST . CR \ 4950

\ channels are pooled and can be freed individually (CHAN-FREE)
: CHAN-CHURN 500 0 DO 8 CHAN CHAN-FREE LOOP ;
CHAN-CHURN
S" chan pool = " TYPE S" ok" TYPE CR

\ TASK: sugar -- running the name spawns the body and leaves the handle
VARIABLE TSUM
0 TSUM A!
TASK: ADDER 5 0 DO I TSUM @+! LOOP ;TASK
ADDER JOIN
S" task: = " TYPE TSUM @ . CR              \ 10

\ shared allocation (data space + heap) from many tasks is serialized
: CHURN ( n -- ) 0 ?DO 1 ALLOC DROP 42 , LOOP ;
: RUNCHURN ( -- ) 8 0 DO 200 ['] CHURN 1 SPAWN LOOP  8 0 DO JOIN LOOP ;
RUNCHURN
S" alloc stress = " TYPE S" ok" TYPE CR

\ FORK / ;FORK namespace scope
FORK
  : TEMP 42 ;
  S" fork inside = " TYPE TEMP . CR          \ 42
;FORK
S" fork after = " TYPE S" TEMP" FIND . CR   \ 0
