#include <lambda.h>
using namespace lambda;

FUN(absi,T x){
	return
		choose
		(x)
		(sub (zero) (x))
		(ge (x) (zero));
}

FUN(gran,T x,T y,T z){
	return
		add (absi (sub (z) (y)))
			(add 
				(absi (sub (y) (x)))
				(absi (sub (z) (x))));
}

FUN(partak,T g,T x,T y,T z){
	let x_  = partak (dec (g)) (sub (x) (one)) (y)  (z) ;
	let y_  = partak (dec (g)) (sub (y) (one)) (z)  (x) ;
	let z_  = partak (dec (g)) (sub (z) (one)) (x)  (y) ;
	let res = partak (zero)    (x_)            (y_) (z_);
	return
		pick
		(when (le (x) (y))				(z))
		(when (le (g) (20))				(force (res)))
//		(otherwise						(par (x_) (par (y_) (par (z_) (res)))));
		(otherwise						(peppyApply3 (partak (zero)) (x_) (y_) (z_)));
}

MAIN(T args){
	let x = lindex (args) (0);
	let y = lindex (args) (1);
	let z = lindex (args) (2);
	let res = partak (gran (x) (y) (z)) (x) (y) (z);
	return choose
		(printstr ("Usage: partak <x> <y> <z>\n"))
		((printstr ("tak "), printval (x), printval (y), printval (z), printstr ("= "), printval (res), printstr ("\n")))
		(lt (length (args)) (3));
}

