\ lib/scheme-jit.fs smoke tests: compile a small Scheme expression to native
\ and run it, with the compiled-function record in the object store.

REQUIRE scheme-jit.fs

: RUN1 ( x c-addr u -- tagged ) PARSE1 SYSTEM JIT-RUN1 ;
: SHOW ( x c-addr u -- ) RUN1 FIX> . CR ;

5  >FIX S" (+ x 5)" SHOW            \ 10
5  >FIX S" (+ (* x 2) 3)" SHOW      \ 13
7  >FIX S" (- x 2)" SHOW            \ 5
10 >FIX S" (* (+ x 1) 3)" SHOW      \ 33
S" scheme-jit ok" TYPE CR

SYSTEM OBJECTS-FREE
SYSTEM STORE-FREE
