\ test-scheme.fs - minimal Scheme loaded and exercised via REQUIRE.
REQUIRE scheme.fs

S" (+ 1 2)" SCHEME-EVAL                 \ 3
S" (define (fact n) (if (= n 0) 1 (* n (fact (- n 1)))))" SCHEME-EVAL
S" (fact 6)" SCHEME-EVAL                \ 720
S" (define (map f l) (if (null? l) (list) (cons (f (car l)) (map f (cdr l)))))" SCHEME-EVAL
S" (map (lambda (x) (* x x)) (list 1 2 3 4))" SCHEME-EVAL   \ (1 4 9 16)
S" (let ((a 2) (b 3)) (+ a b))" SCHEME-EVAL                 \ 5
S" (define x 1)" SCHEME-EVAL
S" (set! x 9)" SCHEME-EVAL
S" x" SCHEME-EVAL                       \ 9
S" (and 1 2 3)" SCHEME-EVAL             \ 3
S" (or #f #f 5)" SCHEME-EVAL            \ 5
S" (if (null? (list)) 111 222)" SCHEME-EVAL  \ 111
S" (quote (1 2 . 3))" SCHEME-EVAL       \ (1 2 . 3)
S" (eq? (quote a) (quote a))" SCHEME-EVAL    \ #t
S" (define (add a b) (+ a b))" SCHEME-EVAL
S" (add 20 22)" SCHEME-EVAL             \ 42
