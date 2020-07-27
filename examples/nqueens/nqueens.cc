#include <lambda.h>
using namespace lambda;

Static<Constant<> > threshold(3);

FUN(safe2,T x,T d,T qs){
	let q = head (qs);
	return choose
		(True)
		(
			bool_and ((ne (x) (q)))
				(bool_and (ne (x) (add (q) (d)))
					(bool_and (ne (x) (sub (q) (d)))
						(safe2 (x) (inc (d)) (tail (qs)))))
		)
		(isempty (qs));
}

FUN(safe,T tup){
	return safe2 (snd (tup)) (one) (fst (tup));
}

FUN(gen,T nq,T bs){
	return mapEager (swap) (filter (safe) (combine (bs) (range1 (nq))));
}

FUN(pargen,T nq,T n,T b){
	let bs = mapPar (pargen (nq) (inc (n))) (gen (nq) (front (b) (end)));
	return
		pick
		(when (ge (n) (threshold)) (lindex (iterate (gen (nq)) (front (b) (end))) (sub (nq) (n))))
		(otherwise (concat (bs)));
}

FUN(nqueens,T nq){
	return length (pargen (nq) (zero) (end));
}

MAIN(T args){
	return
		choose
		(printstr ("Usage: nqueens <board_size>\n"))
		(print (nqueens (head (args))))
		(isempty (args));
}

