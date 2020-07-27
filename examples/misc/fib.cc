/*
Fibonacci, fast/sequential vs. dumb/parallel implementation
Usage: fib <index>
*/
#include <lambda.h>
using namespace lambda;

FUN(fib,T ix){
	let a = fib (dec (ix));
	let b = fib (dec (dec (ix)));
	return
		pick
		(when (eq (ix) (zero))	(zero))
		(when (eq (ix) (one))	(one))
		(when (gt (ix) (20))	(add (par (b) (a)) (b)))
		(otherwise				(add (a) (b)));
}

FUN(fibs){
	return zero |= one |= zipWith (add) (fibs) (tail (fibs));
}

FUN(fib_fast,T ix){
	return lindex (fibs) (ix);
}

MAIN(T args){
	let n = head (args);
	return
		printstr ("reference: "),
		print (fib_fast (n)),
		printstr ("parallel:  "),
		print (fib (n));
}

