#include <lambda.h>
using namespace lambda;

Let<lcmpz_t> encrypt_n("2036450659413645137870851576872812267542175329986469156678671505255564383842535488743101632280716717779536712424613501441720195827856504007305662157107");
Let<lcmpz_t> encrypt_e("387784473137902876992546516170169092918207676456888779623592396031349415024943784869634893342729620092877891356118467738167515879252473323905128540213");

FUN(maximum,T a,T b){return choose (a) (b) (ge (a) (b)); }
FUN(minimum,T a,T b){return choose (a) (b) (le (a) (b)); }

FUN(replicated,T times,T x){
	return tuple (times) (x);
}
FUN(replTake,T n,T r){
	return replicate (minimum (fst (r)) (n)) (snd (r));
}
FUN(replDrop,T n,T r){
	return replicated (maximum (zero) (sub (fst (r)) (n))) (snd (r));
}
FUN(replIsEmpty,T r){
	return eq (fst (r)) (zero);
}

FUN(collect,T n,T xs){
	return
		pick
		(when (eq (n) (0))			(end))
		(when (replIsEmpty (xs))	(end))
//		(otherwise					(front (take (n) (xs)) (collect (n) (drop (n) (xs)))));
		(otherwise					(front (replTake (n) (xs)) (collect (n) (replDrop (n) (xs)))));
}

FUN(size,T n){
	return divide (mult (length (string2chars (show (n)))) (47)) (100);
}

FUN(accum,T x,T y){
	return add (mult (x) (128)) (y);
}

FUN(code){
	return foldl (accum) (bigzero);
}

FUN(sqr,T x){
	return mult (x) (x);
}

FUN(even,T x){
	return eq (mod (x) (2)) (zero);
}

FUN(power,T n,T m,T x){
	return
		pick
		(when (eq (n) (zero))	(one))
		(when (even (n))		(mod (sqr (power (divide (n) (2)) (m) (x))) (m)))
		(otherwise				(mod (mult (x) (power (dec (n)) (m) (x))) (m)));
}

FUN(mapParChunk,T c,T f,T l){
	let chunk = par1f (mapEager (f) (take (c) (l)));
	let rest  = mapParChunk (c) (f) (drop (c) (l));
	return
		pick
		(when (isempty (l))		(end))
		(otherwise				((chunk, rest, concat2 (chunk) (rest))));
}

FUN(encryptRSA,T n,T e,T xs){
	return
//		mapPar
		mapParChunk (5)
			(compose (show) (compose (power (e) (n)) (code)))
			(collect (size (n)) (xs));
}

FUN(printlines,T ss){
	return
		choose
		(nothing)
		((printval (head (ss)), printstr ("\n"),printlines (tail (ss))))
		(isempty (ss));
}

MAIN(T args){
	return
		choose
		(printstr ("Usage: prsa <text_length>\n"))
		(printlines (encryptRSA (encrypt_n) (encrypt_e) (replicated (head (args)) ('x'))))
		(isempty (args));
}

