\ Scheme runtime on the object store: GC stress without deep C recursion
REQUIRE scheme.fs

: CHECK { f a u -- }
  f 0= IF S" FAIL: " TYPE a u TYPE CR -1 THROW THEN ;

\ evaluate a single form and return its value instead of printing it
: XX { c-addr u -- x }
  c-addr S-IN ! u S-LEN ! 0 S-POS ! S-SKIP READ NIL EVAL ;

S" (define (build n) (if (= n 0) (list) (cons n (build (- n 1)))))" SCHEME-EVAL
S" (define big (build 200))" SCHEME-EVAL
SYSTEM GC
SYSTEM OBJECTS-LIVE CONSTANT LIVE0
S" (car big)=" TYPE S" (car big)" XX FIX> . CR   \ 200

\ allocate lots of short-lived pairs with the GC threshold set low
20000 SYSTEM S-LIMIT S!
: CHURN { n -- } n 0 ?DO S" (list 1 2 3 4 5 6 7 8 9 10)" XX DROP LOOP ;
3000 CHURN
SYSTEM GC
SYSTEM OBJECTS-LIVE LIVE0 64 + <= S" garbage collected" CHECK
S" (car big)=" TYPE S" (car big)" XX FIX> . CR   \ still 200
S" (car big)" XX FIX> 200 = S" list intact" CHECK

\ deep non-tail recursion stresses environment frames; GC must keep them
500000 SYSTEM S-LIMIT S!
S" (define (sum n) (if (= n 0) 0 (+ n (sum (- n 1)))))" SCHEME-EVAL
S" (sum 2000)" XX FIX> 2001000 = S" deep recursion" CHECK
SYSTEM GC
SYSTEM OBJECTS-VERIFY DROP

S" scheme-gc ok" TYPE CR

SYSTEM STORE-FREE
