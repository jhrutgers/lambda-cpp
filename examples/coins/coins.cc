#include <lambda.h>
using namespace lambda;

// coins V6
FUN(payN,T val,T coins){
	let first_coin = head (coins);
	let rest_coins = tail (coins);
	let c = fst (first_coin);
	let q = snd (first_coin);
	let coins_ = choose (rest_coins) (front (tuple (c) (dec (q))) (rest_coins)) (eq (q) (one));

	let left = payN (sub (val) (c)) (coins_);
	let right = payN (val) (rest_coins);

	return
		pick
		(when (eq (val) (zero))			(one))
		(when (isempty (coins))			(zero))
		(when (gt (c) (val))			(force (payN (val) (rest_coins))))
		(otherwise						(force (add (left) (right))));
}

// coins V7
FUN(payN_par,T depth,T val,T coins){
	let first_coin = head (coins);
	let rest_coins = tail (coins);
	let c = fst (first_coin);
	let q = snd (first_coin);
	let coins_ = choose (rest_coins) (front (tuple (c) (dec (q))) (rest_coins)) (eq (q) (one));

	let left = payN_par (choose (dec (depth)) (depth) (eq (q) (one))) (sub (val) (c)) (coins_);
	let right = payN_par (dec (depth)) (val) (rest_coins);

//	let res = (par (right) (left), add (left) (right));
//	let res = (par (left) (right), add (left) (right));
//	let res = peppyApply2 (add) (left) (right);
	let res = peppyApply2 (add) (right) (left);

	return
		pick
		(when (eq (depth) (zero))		(payN (val) (eagerList (coins))))
		(when (eq (val) (zero))			(one))
		(when (isempty (coins))			(zero))
		(when (gt (c) (val))			(payN_par (depth) (val) (rest_coins)))
		(otherwise						(res));
}

MAIN(T args){
	let vals	= 250 |= 100 |=  25 |=  10 |=   5 |=   1 |= end;
	let quants	=  55 |=  88 |=  88 |=  99 |= 122 |= 177 |= end;
	let coins1	= eagerList (zip (vals) (quants));
	return
		choose
		(printstr ("Usage: coins <value>\n"))
		(print (payN_par (4) (head (args)) (coins1)))
		(isempty (args));
}

