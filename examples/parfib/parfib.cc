#include <lambda.h>
using namespace lambda;

// sequential fib
FUN(nfib,T x){
	return
		choose
		(one)
		(add
			(add (nfib (sub (x) (two)))
				 (nfib (sub (x) (one))))
			(one))
		(le (x) (one));
//	usleep(100000);
//	return x;
}

// parallel fib till threshold t
FUN(parfib,T n,T t){
	let n1 = parfib (sub (n) (one)) (t);
	let n2 = parfib (sub (n) (two)) (t);
	return
		pick
		(when (le (n) (t))		(force (nfib (n))))
		(otherwise				(par (n2) ((n1, add (add (n1) (n2)) (one)))));
}

FUN(peppyfib,T n,T t){
	let n1 = peppyfib (sub (n) (one)) (t);
	let n2 = peppyfib (sub (n) (two)) (t);
	return
		pick
		(when (le (n) (t))		(force (nfib (n))))
		(otherwise				(peppyApply2 (add (inc)) (n1) (n2)));
}

// determine amount of par
FUN(countPars,T n,T t){
	let n1 = countPars (sub (n) (one)) (t);
	let n2 = countPars (sub (n) (two)) (t);
	return
		pick
		(when (le (n) (t))		(zero))
		(otherwise				(add (add (n1) (n2)) (one)));
}

FUN(thr,T n){
	let t = sub (n) (find (le (dec (parWorkers))) (map (compose (countPars (n)) (sub (n))) (iterate (inc) (zero))));
	return
		choose
		(one)
		(t)
		(lt (t) (one));
}

// nofib!!1
// Usage: parfib <index> <par_threshold>
MAIN(T args){
	let n = lindex (args) (0);
	let t = lindex (args) (1);
//	let t = thr (n);
//	let res = parfib   (n) (t);
	let res = peppyfib (n) (t);
	return
		printstr ("parfib "),
		printval (n),
		printval (t),
		printstr ("= "),
		printval (res),
		printstr ("\n");
}

