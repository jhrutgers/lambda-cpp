#include <lambda.h>
using namespace lambda;

FUN(sort,T l){
	let x		= head (l);
	let xs		= tail (l);
	let losort  = sort (filter (flip (lt) (x)) (xs));
	let hisort  = sort (filter (flip (ge) (x)) (xs));
	let r		= concat2 (losort) (front (x) (hisort));
	return pick
		(when (isempty (l))					(end))
		(when (isempty (xs))				(l))
		(otherwise							(r));
}

Static<Constant<> > valRange(100000);

FUN(quicksortD,T currentDepth,T limit,T l){
	let x		= head (l);
	let xs		= tail (l);
	let lo		= eagerList (filter (gt (x)) (xs));
	let hi		= eagerList (filter (le (x)) (xs));
	let losort  = eagerList (quicksortD (inc (currentDepth)) (limit) (lo));
	let hisort  = eagerList (quicksortD (inc (currentDepth)) (limit) (hi));
//	let cmp		= protect (gt (length (lo)) (length (hi)));
	let cmp		= True;//par (lo) (gt (length (lo)) (length (hi))); // True = losort is longer than hisort
	let losortw = choose (losort) (par1f (losort)) (cmp);
	let hisortw = choose (par1f (hisort)) (hisort) (cmp);
	let longest = choose (losortw) (hisortw) (cmp);
	let shortest= choose (hisortw) (losortw) (cmp);
	let r		= concat2 (losortw) (front (x) (hisortw));
	return 
		pick
		(when (isempty (l))					(end))
		(when (isempty (xs))				(l))
		(when (gt (currentDepth) (limit))	(sort (l)))
		(otherwise							((shortest, longest, r)));
}

FUN(isInRange,T lo,T hi,T val){
	return bool_and (le (lo) (val)) (gt (hi) (val));
}

FUN(isLongList,T length,T l){
	return
		pick
		(when (isempty (l))			(False))
		(when (eq (length) (zero))	(True))
		(otherwise					(isLongList (dec (length)) (tail (l))));
}

FUN(quicksortP,T currentDepth,T limit,T l){
	let x		= sort (take (6) (l));
	let lists	= inc (length (x));
	let rangef	= zipWith (isInRange) (front (zero) (x)) (concat2 (x) (front (inc (valRange)) (end)));
	let filtered= zipWith (filter) (rangef) (replicate (lists) (l));
	let sorted	= mapPar (compose (eagerList) (quicksortP (inc (currentDepth)) (limit))) (eagerList (filtered));
	let r		= concat (sorted);
	
	return
		pick
//		(when (isempty (l))					(end))
//		(when (gt (currentDepth) (limit))	(eagerList (sort (l))))
//		(otherwise							(r));
		(when (isLongList (1000) (l))		(r))
		(otherwise							(eagerList (sort (l))));
}

Static<Constant<> > size(10000);
Static<Constant<> > depth(3);

FUN(eagerConcat,T l1,T l2){
	let h = head (l1);
	let rest = eagerConcat (tail (l1)) (l2);
	return choose
		(l2)
		((h, rest, front (h) (rest)))
		(isempty (l1));
}

FUN(parEagerConcat,T l1,T l2){
	return par1 (eagerConcat (l2) (l1));
}

FUN(parRandomRs,T lo,T hi,T gen,T count){
	let generators	= eagerList (take (parWorkers) (map (mkStdGen) (randoms (gen))));
	let lengths_tail= replicate (dec (parWorkers)) (divide (count) (parWorkers));
	let lengths		= eagerList (front (sub (count) (sum (lengths_tail))) (lengths_tail));
	let randlists	= mapPar (eagerList) (zipWith (take) (lengths) (map (randomRs (lo) (hi)) (generators)));
	let rands		= concat (randlists);
	return rands;
}

// Usage: quicksort [input size]
MAIN(T args){
	let s = choose (size) (head (args)) (isempty (args));
//	let input	= (take (s) (randomRs (zero) (100000) (mkStdGen (42))));
	let input	= parRandomRs (zero) (valRange) (mkStdGen (42)) (s);
	let r		= quicksortP/*D*/ (zero) (depth) (input);
	return
		printstr ("QuickSortD size="),
		printval (s),
		printstr ("depth="),
		printval (depth),
		printstr ("\n"),
//		printlist (input),
//		eagerList (input),
//		printlist (input),
//		printlist (r),
//		printlist (sort (input)),
		printstr ("Sum of sort: "),
		par (r) (printval (sum (input))),
		printval (sum (r)),
		printstr ("\n");
}

