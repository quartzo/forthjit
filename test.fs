\ ---- Forth smoke tests ----

\ stack + arithmetic
1 2 + . CR                    \ 3
10 3 - . CR                   \ 7
4 5 * . CR                    \ 20
7 2 / . CR                    \ 3
7 2 MOD . CR                  \ 1
-5 ABS . CR                   \ 5

\ comparisons (true = -1)
1 1 = . CR                    \ -1
1 2 = . CR                    \ 0
3 4 < . CR                    \ -1

\ colon word + literal
: square DUP * ;
5 square . CR                 \ 25

\ IF / ELSE / THEN
: sign DUP 0< IF DROP -1 ELSE 0> IF 1 ELSE 0 THEN THEN ;
5 sign . CR                   \ 1
-5 sign . CR                  \ -1
0 sign . CR                   \ 0

\ recursion
: fact DUP 1 <= IF DROP 1 ELSE DUP 1- RECURSE * THEN ;
6 fact . CR                   \ 720

: fib DUP 2 < IF EXIT THEN DUP 1- RECURSE SWAP 2 - RECURSE + ;
10 fib . CR                   \ 55

\ BEGIN / UNTIL
: countdown BEGIN DUP . 1- DUP 0= UNTIL DROP ;
5 countdown CR                \ 5 4 3 2 1

\ BEGIN / WHILE / REPEAT
: halves BEGIN DUP 1 > WHILE DUP . 2 / REPEAT DROP ;
32 halves CR                  \ 32 16 8 4 2

\ DO / LOOP with I
: sumto 0 SWAP 1+ 1 DO I + LOOP ;
10 sumto . CR                  \ 55

\ nested loops with I and J
: table 3 1 DO 3 1 DO I J * . LOOP LOOP ;
table CR

\ +LOOP
: evens 10 0 DO I . 2 +LOOP ;
evens CR                      \ 0 2 4 6 8

\ variables
VARIABLE counter
0 counter !
counter @ 1+ counter !
counter @ . CR                \ 1

\ constants
42 CONSTANT answer
answer . CR                   \ 42

\ hex literal
$FF . CR                      \ 255
0x10 . CR                     \ 16

CR
