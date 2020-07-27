/*
Copyright 2013 Jochem H. Rutgers (j.h.rutgers@utwente.nl)

This file is part of lambda.

lambda is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

lambda is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with lambda.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __LAMBDA_LIB_H
#define __LAMBDA_LIB_H

#include <stdio.h>
#include <math.h>
#include <lambda/debug.h>
#include <lambda/term.h>
#include <lambda/worker.h>

////////////////////////////////////
////////////////////////////////////
// Standard library
////////////////////////////////////
////////////////////////////////////

namespace lambda {

////////////////////////////////////
// Basics

#ifdef LAMBDA_DEBUG
#  define LAMBDA_FUNC_LABEL(f,arg...) STRINGIFY(f) STRINGIFY((arg))
#else
#  define LAMBDA_FUNC_LABEL(...) NULL
#endif

#define FUN_DECL(f,arg...)											\
static lambda::Term_tref f##_func(arg);								\
lambda::Function f ATTR_SHARED_ALIGNMENT (f##_func, LAMBDA_FUNC_LABEL(f,##arg));

#define FUN_IMPL(f,arg...)											\
static lambda::Term_tref f##_func(arg)

#define FUN(f,arg...)		FUN_DECL(f,##arg) FUN_IMPL(f,##arg)
	
#define MAIN_DECL(arg...)	FUN_DECL(lc_main,##arg)
#define MAIN(arg...)		FUN_IMPL(lc_main,##arg)

#define EAGER_FUN(f,arg...)											\
static lambda::Term_tref f(arg) __attribute__((unused));			\
static lambda::Term_tref f(arg)

template <typename A> const A& as(T x){ return x.Compute<A>(); }

// basic functions
Static<Constant<> > nothing(0);
Static<Constant<> > zero(0);
Static<Constant<> > one(1);
Static<Constant<> > two(2);
#ifdef HAVE_GMP
Let<lcmpz_t> bigzero(0);
FUN(mpzInitI,T c){				return *new Global<Constant<lcmpz_t> >(as<mpzInit_t>(c).i); }
FUN(mpzInitS,T c){				return *new Global<Constant<lcmpz_t> >(as<mpzInit_t>(c).str); }
#endif

FUN(id,T t){					return t; }
FUN(apply,T f,T a){				return f (a.FollowFullIndirection()); }
FUN(flip,T f,T a,T b){			return f (b) (a); }
FUN(compose,T f,T g,T a){		return f (g (a)); }
FUN(trash,T a,T b){				return b; }
FUN(trash2,T a,T b){			return a; }
static bool isReducable(T x){	return x.IsReducable(); }
static bool isFunction(T x){	return x.Arguments()>0; }
static bool isBlocking(T x){	return x.IsBlocked(); }

// evaluation
//
// Increasing amount of eagerness:
// - reduce: stops at non-reducable,     blocking, and lazy terms (determined by Term::FullReduce())
// - stress: stops at non-reducable, and blocking terms
// - force:  stops at non-reducable terms
// - eager:  stops at non-reducable terms, but is executed immediately

FUN(lazy,T x){
	LAMBDA_ASSERT(worker_eval_stack().peek()!=NULL&&worker_eval_stack().peek()->term!=NULL,"evaluating empty program");
	LAMBDA_ASSERT(&worker_eval_stack().peek()->term->BaseFunction()==&lazy,"popping non-lazy %s",worker_eval_stack().peek()->term->name().c_str());
	EvalTerm::eval_mode_t& e=worker_eval_mode();
	if(e<=EvalTerm::eval_normal){
		LAMBDA_PRINT(eval,"stopping full reduction on lazy term %s",x.name().c_str());
		e=EvalTerm::eval_stop;
	}else
		LAMBDA_PRINT(eval,"lazy ignored; term %s is evaluated eagerly",x.name().c_str());
	return x;
}
FUN(block,T x){
	LAMBDA_ASSERT(worker_eval_stack().peek()!=NULL&&worker_eval_stack().peek()->term!=NULL,"evaluating empty program");
	LAMBDA_ASSERT(&worker_eval_stack().peek()->term->BaseFunction()==&block,"popping non-blocked %s",worker_eval_stack().peek()->term->name().c_str());
	EvalTerm::eval_mode_t& e=worker_eval_mode();
	if(e<=EvalTerm::eval_stressed){
		LAMBDA_PRINT(eval,"stopping full reduction on blocked term %s",x.name().c_str());
		e=EvalTerm::eval_stop;
	}else
		LAMBDA_PRINT(eval,"block ignored; term %s is evaluated eagerly",x.name().c_str());
	return x;
}
FUN(stop,T x){
	LAMBDA_ASSERT(worker_eval_stack().peek()!=NULL&&worker_eval_stack().peek()->term!=NULL,"evaluating empty program");
	LAMBDA_ASSERT(&worker_eval_stack().peek()->term->BaseFunction()==&stop,"popping non-stopped %s",worker_eval_stack().peek()->term->name().c_str());
	EvalTerm::eval_mode_t& e=worker_eval_mode();
	if(e<=EvalTerm::eval_forced){
		LAMBDA_PRINT(eval,"stopping full reduction on stopped term %s",x.name().c_str());
		e=EvalTerm::eval_stop;
	}else
		LAMBDA_PRINT(eval,"stop ignored; term %s is evaluated eagerly",x.name().c_str());
	return x;
}
// stop reduction on whole eval stack of all non-main workers, except for the first term (usually a blackhole or global finisher)
FUN(halt,T x){
	LAMBDA_ASSERT(worker_eval_stack().peek()!=NULL&&worker_eval_stack().peek()->term!=NULL,"evaluating empty program");
	if(worker_id()!=0)
		worker_eval_setmode(EvalTerm::eval_halt);
	return x;
}
FUN(catchHalt,T x){
	LAMBDA_ASSERT(worker_eval_stack().peek()!=NULL&&worker_eval_stack().peek()->term!=NULL,"evaluating empty program");
	if(worker_id()!=0){
		EvalTerm::eval_mode_t& e=worker_eval_mode();
		if(e!=EvalTerm::eval_halt)
			e=EvalTerm::eval_catch;
	}
	return x;
}
FUN(lazy_stop,T t){
	return t;
}
FUN(lazyApply,T f,T a){
	return lazy (f (a));
}

FUN(reduce,T t){
	LAMBDA_PRINT(eval,"pushing %s on eval stack",t.name().c_str());
	worker_eval_stack().push(EvalTerm(&t,EvalTerm::eval_normal));
	return t;
}
FUN(reducedApply,T f,T a){
	LAMBDA_PRINT(eval,"pushing %s on eval stack",a.name().c_str());
	worker_eval_stack().push(EvalTerm(&a,EvalTerm::eval_normal));
	return apply (f) (a);
}

FUN(stress,T x){
	LAMBDA_PRINT(eval,"pushing stressed %s on eval stack",x.name().c_str());
	worker_eval_stack().push(EvalTerm(&x,EvalTerm::eval_stressed));
	return x;
}
FUN(stressedApply,T f,T a){
	LAMBDA_PRINT(eval,"pushing stressed %s on eval stack",a.name().c_str());
	worker_eval_stack().push(EvalTerm(&a,EvalTerm::eval_stressed));
	return apply (f) (a);
}
// evaluate all the way immediately
EAGER_FUN(eager,T t){
	LAMBDA_PRINT(eval,"pushing forced %s on eval stack",t.name().c_str());
	worker_eval_stack().push(EvalTerm(&t,EvalTerm::eval_forced));
	return t;
}
FUN(eagerApply,T f,T a){
	return apply (f) (eager (a));
}
FUN(force,T t){
	return eager (t);
}
FUN(inheritApply,T w,T f,T a){
	switch(worker_eval_mode()){
	case EvalTerm::eval_normal:
		return w (reducedApply (f) (a));
	case EvalTerm::eval_stressed:
		return w (stressedApply (f) (a));
	case EvalTerm::eval_forced:
		return w (eagerApply (f) (a));
	default:
		return w (apply (f) (a));
	}
}

FUN(opportune,T a,T b){			return reduce (a.TryReduce(b)); }

// parallellism
Static<Constant<> > parWorkers((lcint_t)Config::workers);
FUN(globalize,T t){				return Config::workers>1?(T)t.Globalize():t; }
EAGER_FUN(protect,T t){			return Config::workers>1?*new Blackhole(t):t; }
EAGER_FUN(par1,T x){
	if(Config::workers==1)
		// apparently, x is important, so reduce it eagerly (which might reduce memory usage)
		return reduce (x);
	else{
		let e=Worker::Enqueue(x);
		LAMBDA_PRINT(par,"enqueued %s",e.term().name().c_str());
		return lazy (e);
	}
}
FUN(par1f,T x){					return par1 (x); }
FUN(par,T x){					return par1 (x), id; }
FUN(parApply,T f,T a){			return par1 (f (a)); }
FUN(pseq,T x){					return reducedApply (trash) (x); }
FUN(pass,T f,T a){				return pseq (f (a)) (a); }
FUN(gc,T x){					return current_worker->GetHeap().DoGC(true),x;}
FUN(stall,T x){
	worker_sleep();
	return x;
}

Term_tref operator,(T lhs,T rhs){					return pseq (lhs) (rhs); }
Term_tref operator,(T lhs,lcint_t rhs){				return pseq (lhs) (rhs); }
Term_tref operator,(T lhs,lcfloat_t rhs){			return pseq (lhs) (rhs); }
Term_tref operator,(T lhs,lccomplex_t rhs){			return pseq (lhs) (rhs); }
Term_tref operator,(T lhs,const Term_tref& rhs){	return pseq (lhs) (rhs); }

FUN(postpone_stop,T x){	return x; }
FUN_DECL(postponed,T x,T bh)
FUN(postpone,T x,T bh){
	if(worker_id()==0){
		return x;//reduce (x);
	}else if(!isReducable(bh)){
//		LAMBDA_PRINT(par,"postpone: %s is not reducable, continue evaluating %s",bh.name().c_str(),x.name().c_str());
		return x;//reduce (x);
	}else if(!isBlocking(bh)){
//		LAMBDA_PRINT(par,"postpone: %s is not blocking, evaluating it first",bh.name().c_str());
		return stressedApply (postpone (x)) (bh);
	}else{
		current_worker->Enqueue(postponed (x) (bh),false,1);
		Stats<>::Postponed();
		LAMBDA_PRINT(par,"postpone: %s blocks, enqueue and lazify %s",bh.name().c_str(),x.name().c_str());
		return halt (x);
	}
}
FUN_IMPL(postponed,T x,T bh){
	LAMBDA_ASSERT(worker_id()!=0,"worker 0 should not pop postponed terms");
	if(isBlocking(bh)){
		LAMBDA_PRINT(par,"postponing again %s; %s still blocks",x.name().c_str(),bh.name().c_str());
		//prevent respawning too quickly
		return stall (postpone (x) (bh)), nothing;
	}else{
		LAMBDA_PRINT(par,"reducing postponed %s; %s does not block anymore",x.name().c_str(),bh.name().c_str());
		return x, nothing;
	}
}
FUN(postponedApplyNow,T x,T bh){ // x is assumed to be an application of bh
	if(worker_id()==0 || !isReducable(bh))
		return x;
	else if(!isBlocking(bh)){
		// reduce bh and try again
		return stressedApply (postponedApplyNow (x)) (bh);
	}else{
		if(worker_eval_mode()<=EvalTerm::eval_stressed){
			LAMBDA_PRINT(par,"stopping eval of postponed %s; %s blocks",x.name().c_str(),bh.name().c_str());
			return halt (postponedApplyNow (x) (bh)); // the blackhole prevents postponedApplyNow to be called before bh unblocks
		}else{
			LAMBDA_PRINT(par,"enforcing postponed %s, although %s blocks",x.name().c_str(),bh.name().c_str());
			// somehow really required to go stall on the bh
			return x;
		}
	}
}
FUN(postponedApply,T f,T a){
	if(worker_id()==0 || !isReducable(a))
		return f (a);
	else if(!isBlocking(a)){
		return stressedApply (postponedApply (f)) (a);
	}else
		return postpone (postponedApplyNow (protect (f (a))) (a)) (a);
}
// parallel eagerly partial postponed apply
FUN(peppyApply2,T f,T a,T b){
	let fa = f (a);
	return a, fa, postponedApply (trash2 (postponedApply (fa) (par1 (b)))) (a);
}
FUN(peppyApply3,T f,T a,T b,T c){
	let b_ = par1 (b);
	let c_ = par1 (c);
	let fa = f (a);
	return a, fa, postponedApply (trash2 (postponedApply (postponedApply (fa) (b_)) (c_))) (a);
}

// misc
Static<Constant<> >& False=zero;
Static<Constant<> >& True=one;

FUN(choose,T nzero,T zero,T expr){	
	if(isReducable(expr))
		return eagerApply (choose (nzero) (zero)) (expr);
	else if(isFunction(expr))
		return compose (choose (nzero) (zero)) (expr);
	else
		return as<lcint_t>(expr)?nzero:zero;
}

FUN(isTerm,T toCompareWith,T toCompare){
	return &toCompareWith.FollowFullIndirection() == &toCompare.FollowFullIndirection() ? True : False;
}

FUN(isReducedTerm,T toCompareWith,T toCompare){
	if(isReducable(toCompareWith))
		return eagerApply (flip (isReducedTerm) (toCompare)) (toCompareWith);
	else if(isReducable(toCompare))
		return eagerApply (isReducedTerm (toCompareWith)) (toCompare);
	else
		return &toCompareWith == &toCompare? True:False;
}

FUN(isTrue,T expr){				return isReducedTerm (True) (expr); }
FUN(isFalse,T expr){			return isReducedTerm (False) (expr); }
FUN(isZero,T expr){
	if(isReducable(expr))
		return eagerApply (isZero) (expr);
	else if(isFunction(expr))
		return compose (isZero) (expr);
	else if(as<lcint_t>(expr)==0)
		return True;
	else
		return False;
}

FUN(isNonZero,T expr){
	if(isReducable(expr))
		return eagerApply (isNonZero) (expr);
	else if(isFunction(expr))
		return compose (isNonZero) (expr);
	else if(as<lcint_t>(expr)!=0)
		return True;
	else
		return False;
}

// makes sure op (a1) (a2) is called when a1 and a2 are expanded to constants
FUN(primop,T op,T a1,T a2){
	if(isReducable(a1))
		return eagerApply (flip (primop (op)) (a2)) (a1);
	else if(isReducable(a2))
		return eagerApply (primop (op) (a1)) (a2);
	else if(isFunction(a1))
		return compose (flip (primop (op)) (a2)) (a1);
	else if(isFunction(a2))
		return compose (primop (op) (a1)) (a2);
	else
		return op (a1) (a2);
}

FUN(blackhole_finish,T bh,T x){
	return block ((static_cast<Blackhole*>(&bh))->Finish(x));
}
FUN(blackhole_finisher,T bh,T x){
	return catchHalt (reducedApply (blackhole_finish (bh)) (x));
}

FUN(global_reduce_finish,T g,T r){
	let res=*(static_cast<Global<Application>*>(&g))->SetIndirection(&r.Globalize());
	return block (res);
}
FUN(global_reduce_finisher,T g,T r){
	return inheritApply (catchHalt) (global_reduce_finish (g)) (r);
}

////////////////////////////////////
// Boolean logic

//FUN(primand,T a,T b){	return &a == &True && &b == &True? True:False; }
//FUN(primor,T a,T b){	return &a == &True || &b == &True? True:False; }
FUN(primxor,T a,T b){	return &a == &b? True:False; }
//FUN(primnot,T a,T b){	return &a == &False? True:False; }

FUN(bool_xor,T a,T b){	return primop (primxor) (a) (b); }
FUN(bool_and,T a,T b){
	if(isReducable(a))
		return eagerApply (flip (bool_and) (b)) (a);
	else if(isFunction(a))
		return compose (flip (bool_and) (b)) (a);
	else if(&a==&False)
		return False;
	else
		return b;
}
FUN(bool_or,T a,T b){
	if(isReducable(a))
		return eagerApply (flip (bool_or) (b)) (a);
	else if(isFunction(a))
		return compose (flip (bool_or) (b)) (a);
	else if(&a==&True)
		return True;
	else
		return b;
}
FUN(bool_not,T a){
	if(isReducable(a))
		return eagerApply (bool_not) (a);
	else if(&a==&False)
		return True;
	else
		return False;
}

FUN(primeq,T a,T b){	return a==b? True:False; }
FUN(primne,T a,T b){	return a!=b? True:False; }
FUN(primgt,T a,T b){	return a> b? True:False; }
FUN(primlt,T a,T b){	return a< b? True:False; }
FUN(primge,T a,T b){	return a>=b? True:False; }
FUN(primle,T a,T b){	return a<=b? True:False; }

FUN(eq,T a,T b){		return primop (primeq) (a) (b); }
FUN(ne,T a,T b){		return primop (primne) (a) (b); }
FUN(gt,T a,T b){		return primop (primgt) (a) (b); }
FUN(lt,T a,T b){		return primop (primlt) (a) (b); }
FUN(ge,T a,T b){		return primop (primge) (a) (b); }
FUN(le,T a,T b){		return primop (primle) (a) (b); }


////////////////////////////////////
// Matching construct

FUN(otherwise,T x,T result){
	return choose
		(x)
		(result)
		(isTerm (nothing) (result));
}

FUN_DECL(next_when,T result,T otherw)

FUN(when,T expr,T nzero,T result){
	return choose 
		(next_when (nzero))
		(next_when (result))
		(bool_and (isTerm (nothing) (result)) (isNonZero(expr)));
}

FUN_IMPL(next_when,T result,T otherw){
	LAMBDA_ASSERT(&otherw.BaseFunction().BaseFunction()==&when||&otherw.BaseFunction().BaseFunction()==&otherwise,
		"invalid pick-when-otherwise construct: %s",otherw.name().c_str());
	return otherw (result);
}


FUN(pick,T w){
	LAMBDA_ASSERT(&w.BaseFunction().BaseFunction()==&when||&w.BaseFunction().BaseFunction()==&otherwise,
		"invalid pick-when-otherwise construct: %s",w.name().c_str());
	return w (nothing);
}

////////////////////////////////////
// Math

Static<Constant<lcfloat_t> > zero_f(0.0);
Static<Constant<lcfloat_t> > one_f(1.0);
Static<Constant<lccomplex_t> > zero_c(0.0);
Static<Constant<lccomplex_t> > one_c(1.0);
Static<Constant<lccomplex_t> > i_c(I);

FUN(primadd,T a1,T a2){			return a1+a2; }
FUN(primsub,T a1,T a2){			return a1-a2; }
FUN(primmult,T a1,T a2){		return a1*a2; }
FUN(primdiv,T a1,T a2){			return a1/a2; }
FUN(primmod,T a1,T a2){			return a1%a2; }

FUN(add,T a1,T a2){				return primop (primadd) (a1) (a2); }
FUN(sub,T a1,T a2){				return primop (primsub) (a1) (a2); }
FUN(mult,T a1,T a2){			return primop (primmult) (a1) (a2); }
FUN(divide,T a1,T a2){			return primop (primdiv) (a1) (a2); }
FUN(mod,T a1,T a2){				return primop (primmod) (a1) (a2); }
FUN(inc,T t){					return add (t) (one); }
FUN(dec,T t){					return sub (t) (one); }

FUN(iszerof,T f){
	if(isReducable(f))
		return eagerApply (iszerof) (f);
	else if(isFunction(f))
		return compose (iszerof) (f);
	else{
		lcfloat_t c=as<lcfloat_t>(f);
		return c>-Config::epsilon&&c<Config::epsilon?True:False;
	}
}

FUN(isonef,T f){
	if(isReducable(f))
		return eagerApply (isonef) (f);
	else if(isFunction(f))
		return compose (isonef) (f);
	else{
		lcfloat_t c=as<lcfloat_t>(f);
		return c>1.0-Config::epsilon&&c<1.0+Config::epsilon?True:False;
	}
}

FUN(intToFloat,T x){
	if(isReducable(x))
		return eagerApply (intToFloat) (x);
	else if(isFunction(x))
		return compose (intToFloat) (x);
	else{
		lcfloat_t f=(lcfloat_t)as<lcint_t>(x);
		return *new Constant<lcfloat_t>(f);
	}
}

FUN(floatToInt,T x){
	if(isReducable(x))
		return eagerApply (floatToInt) (x);
	else if(isFunction(x))
		return compose (floatToInt) (x);
	else{
		lcint_t i=(lcint_t)lround(as<lcfloat_t>(x));
		return *new Constant<lcint_t>(i);
	}
}

FUN(floatToComplex,T x){
	if(isReducable(x))
		return eagerApply (floatToComplex) (x);
	else if(isFunction(x))
		return compose (floatToComplex) (x);
	else{
		lccomplex_t c=(lccomplex_t)as<lcfloat_t>(x);
		return *new Constant<lccomplex_t>(c);
	}
}

FUN(math_cabs,T x){
	if(isReducable(x))
		return eagerApply (math_cabs) (x);
	else if(isFunction(x))
		return compose (math_cabs) (x);
	else{
		lcfloat_t f=cabs(as<lccomplex_t>(x));
		return *new Constant<lcfloat_t>(f);
	}
}

FUN(math_sin,T x){
	if(isReducable(x))
		return eagerApply (math_sin) (x);
	else if(isFunction(x))
		return compose (math_sin) (x);
	else{
		lcfloat_t f=(lcfloat_t)sin(as<lcfloat_t>(x));
		return *new Constant<lcfloat_t>(f);
	}
}

////////////////////////////////////
// Tuples

Static<Constant<> > empty(-1);

FUN(tuple,T left,T right,T c){	return choose (right) (left) (c); }
FUN(fst,T tup){					return tup (zero); }
FUN(snd,T tup){					return tup (one); }
FUN(swap,T tup){				return tuple (snd (tup)) (fst (tup)); }

////////////////////////////////////
// Lists

Static<Constant<> >& end=empty;

FUN(front,T t,T list){			return tuple (t) (list); }
FUN(head,T list){				return fst (list); }
FUN(tail,T list){				return snd (list); }
FUN(isempty,T list){			return isReducedTerm (end) (list); }

FUN(iterate,T f,T start){
	return front (start) (iterate (f) (f (start)));
}

FUN(take,T count,T list){
	return choose
		(end)
		(front (head (list)) (take (dec (count)) (tail (list))))
		(bool_or (isZero (count)) (isempty (list)));
}

FUN(repeat,T val){
	return front (val) (repeat (val));
}

FUN(replicate,T count,T val){
	return take (count) (iterate (id) (val));
}

FUN(range1,T till){
	return take (till) (iterate (inc) (one));
}

FUN(range,T from,T till){
	return take (inc (sub (till) (from))) (iterate (inc) (from));
}

FUN(drop,T count,T list){
	return choose
		(list)
		(drop (dec (count)) (tail (list)))
		(bool_or (isZero (count)) (isempty (list)));
}

FUN(dropWhile,T f,T list){
	return choose
		(list)
		(dropWhile (f) (tail (list)))
		(bool_or (isempty (list)) (bool_not (f (head (list)))));
}

FUN(concat2,T l1,T l2){
	return choose
		(l2)
		(front (head (l1)) (concat2 (tail (l1)) (l2)))
		(isempty (l1));
}

FUN(concat,T ls){
	return choose
		(end)
		(concat2 (head (ls)) (concat (tail (ls))))
		(isempty (ls));
}

FUN(zip,T l1,T l2){
	return
		choose
		(end)
		(front (tuple (head (l1)) (head (l2))) (zip (tail (l1)) (tail (l2))))
		(bool_or (isempty (l1)) (isempty (l2)));
}

FUN(zipWith,T f,T l1,T l2){
	return choose
		(end)
		(front (f (head (l1)) (head (l2))) (zipWith (f) (tail (l1)) (tail (l2))))
		(isempty (l1));
}

FUN(zipAll,T f,T lists){
	let first=head (lists);
	let rest=tail (lists);
	return choose
		(end)
		(choose
			(first)
			(zipWith (f) (first) (zipAll (f) (rest)))
			(isempty (tail (lists)))
		)
		(isempty (lists));
}

FUN(foldl,T f,T start,T list){
	return choose
		(start)
		(foldl (f) (f (start) (head (list))) (tail (list)))
		(isempty (list));
}

FUN(foldl1,T f,T list){
	return foldl (f) (head (list)) (tail (list));
}

FUN(scanl,T f,T a,T l){
	return
		front (a) (
			choose
			(end)
			(scanl (f) (f (a) (head (l))) (tail (l)))
			(isempty (l))
		);
}

FUN(scanl1,T f,T l){
	return
		choose
		(end)
		(scanl (f) (head (l)) (tail (l)))
		(isempty (l));
}

FUN(map,T f,T list){
	return choose
		(end)
		(front (f (head (list))) (map (f) (tail (list))))
		(isempty (list));
}

FUN(mapEager,T f,T list){
/*	let l = map (f) (list);
	return
		foldl (flip (pseq)) (nothing) (l),
		(l);*/
	let h = f (head (list));
	let rest = mapEager (f) (tail (list));
	return choose
		(end)
		((h, rest, front (h) (rest)))
		(isempty (list));
}

FUN(eagerList,T list){
	return mapEager (id) (list);
}

FUN(eagerMatrix,T m){
	return mapEager (eagerList) (m);
}

FUN(mapPar,T f,T list){
	return mapEager (parApply (f)) (list);
}

FUN(sum,T list){
	let first=head (list);
	let rest=tail (list);
	return choose
		(zero)
		(choose
			(first)
			(add (first) (sum (rest)))
			(isempty (rest))
		)
		(isempty (list));
}

FUN(length,T list){
	return foldl (inc (trash2)) (zero) (list);
}

FUN(reverse,T list){
	return choose
		(end)
		(concat2 (reverse (tail (list))) (front (head (list)) (end)))
		(isempty (list));
}

FUN(rotate,T list,T count){
	return concat2 (drop (count) (list)) (take (count) (list));
}

FUN(lindex,T list,T ix){
	return head (drop (ix) (list));
}

FUN(find,T f,T list){
	return choose
		(zero) // beyond length of list when isempty(list)
		(inc (find (f) (tail (list))))
		(bool_or (isempty(list)) (f (head (list))));
}

FUN(findIndices,T cmp,T list){
	let rest=map (inc) (findIndices (cmp) (tail (list)));
	return choose
		(end)
		(choose
			(front (zero) (rest))
			(rest)
			(cmp (head (list)))
		)
		(isempty(list));
}

FUN(splitAt,T at,T xs){
	return tuple (take (at) (xs)) (drop (at) (xs));
}

FUN(any,T f,T list){
	return foldl (bool_or (f)) (False) (list);
}

FUN(all,T f,T list){
	return foldl (bool_and (f)) (True) (list);
}

FUN(filter,T f,T list){
	let h = head (list);
	let rest = filter (f) (tail (list));
	return choose
		(end)
		(choose
			(front (h) (rest))
			(rest)
			(f (h))
		)
		(isempty (list));
}

FUN(combine,T l1,T l2){
	return choose
		(end)
		(
			concat2
			(map (flip (tuple) (head (l2))) (l1))
			(combine (l1) (tail (l2)))
		)
		(isempty (l2));
}


Static<Constant<> >& rlist __attribute__((unused))=end;
//Term_tref operator|(T l,lcint_t e){	return front (e) (l);}
Term_tref operator|(T l,int e){			return front ((lcint_t)e) (l);}
Term_tref operator|(T l,long e){		return front ((lcint_t)e) (l);}
Term_tref operator|(T l,long long e){	return front ((lcint_t)e) (l);}
Term_tref operator|(T l,lcfloat_t e){	return front (e) (l);}
Term_tref operator|(T l,lccomplex_t e){	return front (e) (l);}
Term_tref operator|(T l,T e){			return front (e) (l);}

//Term_tref operator|=(lcint_t e,T l){	return front (e) (l);}
Term_tref operator|=(int e,T l){		return front ((lcint_t)e) (l);}
Term_tref operator|=(long e,T l){		return front ((lcint_t)e) (l);}
Term_tref operator|=(long long e,T l){	return front ((lcint_t)e) (l);}
Term_tref operator|=(lcfloat_t e,T l){	return front (e) (l);}
Term_tref operator|=(lccomplex_t e,T l){return front (e) (l);}
Term_tref operator|=(T e,T l){			return front (e) (l);}

////////////////////////////////////
// Random

FUN(mkStdGen,T seed){
	return seed;
}

FUN(random,T gen){
	if(isReducable (gen))
		return eagerApply (random) (gen);
	else if(isFunction (gen))
		return compose (random) (gen);
	else {
		unsigned int s=as<lcint_t>(gen);
		lcint_t r=rand_r(&s);
		return tuple (r) ((lcint_t) s);
	}
}

FUN(randoms,T gen){
	return map (fst) (iterate (compose (random) (snd)) (random (gen)));
}

FUN(randomR,T lo,T hi,T gen){
	let r = random (gen);
	return tuple (add (mod (fst (r)) (sub (hi) (lo))) (lo)) (snd (r));
}

FUN(randomRs,T lo,T hi,T gen){
	return map (fst) (iterate (compose (randomR (lo) (hi)) (snd)) (randomR (lo) (hi) (gen)));
}

////////////////////////////////////
// Matrices

FUN(m_identity_,T zero,T one,T size){
	let first_row = front (one) (replicate (dec (size)) (zero));
	let inner_m = m_identity_ (zero) (one) (dec (size));
	let other_rows = map (front (zero)) (inner_m);
	let m = front (first_row) (other_rows);

	return choose
		(m)
		(end)
		(size);
}

FUN(m_identity,T size){		return m_identity_ (zero)   (one)   (size);}
FUN(m_identity_f,T size){	return m_identity_ (zero_f) (one_f) (size);}

FUN(m_row,T matrix,T ix){
	return lindex (matrix) (ix);
}

FUN(m_col,T matrix,T ix){
	return map (flip (lindex) (ix)) (matrix);
}

FUN(m_cell,T matrix,T row,T col){
	return lindex (m_row (matrix) (row)) (col);
}

FUN(m_transpose,T matrix){
	let first_col = map (head) (matrix);
	return choose
		(end)
		(front (first_col) (m_transpose (map (tail) (matrix))))
		(bool_or (isempty(matrix)) (isempty(head (matrix))));
}

FUN(m_map,T f,T matrix){
	return map (map (f)) (matrix);
}

FUN(m_zipWith,T f,T m1,T m2){
	return choose
		(end)
		(front 
			(zipWith   (f) (head (m1)) (head (m2)))
			(m_zipWith (f) (tail (m1)) (tail (m2)))
		)
		(isempty(m1));
}

FUN(m_add,T m1,T m2){
	return m_zipWith (add) (m1) (m2);
}

FUN(m_sub,T m1,T m2){
	return m_zipWith (sub) (m1) (m2);
}

FUN(m_mults,T m,T c){
	return m_map (mult (c)) (m);
}

FUN(m_mult_cell_,T row,T col){
	return sum (zipWith (mult) (row) (col));
}

FUN(m_mult_rows_,T rows,T cols){
	return choose
		(end)
		(front
			(map (m_mult_cell_ (head (rows))) (cols))
			(m_mult_rows_ (tail (rows)) (cols))
		)
		(isempty(rows));
}

FUN(m_mult,T m1,T m2){
	let rows=m1;
	let cols=m_transpose (m2);
	return m_mult_rows_ (rows) (cols);
}

FUN(m_swapRow,T m,T ix1,T ix2){
	let pre1 = take (ix1) (m);
	let row1 = m_row (m) (ix2);
	let inter = drop (inc (ix1)) (take (ix2) (m));
	let row2 = m_row (m) (ix1);
	let post2 = drop (inc (ix2)) (m);
	let swapped =
		 concat2 (pre1)
		(front  (row1)
		(concat2 (inter)
		(front  (row2)
				(post2))));

	return choose
		(m)
		(choose
			(m_swapRow (m) (ix2) (ix1))
			(swapped)
			(gt (ix1) (ix2))
		)
		(eq (ix1) (ix2));
}

FUN(m_multRow,T m,T ix,T c){
	let first=take (ix) (m);
	let second=drop (ix) (m);
	return concat2 (first) (front (map (mult (c)) (head (second))) (tail (second)));
}

FUN(m_addRow,T m,T rowToAddTo,T rowToAdd){
	let addr=m_row (m) (rowToAdd);
	let first=take (rowToAddTo) (m);
	let second=drop (rowToAddTo) (m);
	return concat2 (first) (front (zipWith (add) (addr) (head (second))) (tail (second)));
}

FUN(m_cleanRow,T cleaner,T row){
	return zipWith (flip (sub) (mult (head (row)))) (row) (cleaner);
}

FUN(m_rowEchelon,T m){
	let firstrow=head (m);
	let cleaned_rest=map (m_cleanRow (firstrow)) (tail (m));
	let cleaned_m = front (firstrow) (m_rowEchelon (cleaned_rest));
	
	let root = head (head (m));
	
	let one_root_check = choose
		(cleaned_m) // root is one
		(m_rowEchelon (front (map (mult (divide (1.0) (root))) (head (m))) (tail (m)))) // make sure that this row starts with 1
		(isonef(root));
	
	let zero_root_check = choose
		(m_rowEchelon (rotate (m) (one))) // make sure that element m(0,0) != 0
		(one_root_check) // root is non-zero
		(iszerof(root));
	
	let column_zero_check = choose
		(map (front (zero_f)) (m_rowEchelon (map (drop (one)) (m)))) // this column is zero, proceed to next column
		(zero_root_check) // has non-zero in this column
		(all (iszerof) (m_col (m) (zero)));

	let e = choose
		(m) // empty matrix
		(column_zero_check) // have some data
		(bool_or (isempty (m)) (isempty (head (m))));

	return e;
}


FUN(m_rrefWipe_,T row,T theone,T rowToWipe){
	let times=lindex (rowToWipe) (theone);
	let rowMult=map (mult (times)) (row);
	return zipWith (sub) (rowToWipe) (rowMult);
}

FUN(m_rrefWipe,T m){
/*	if(isempty(m) == True || isempty(head (m)) == True)
		// empty matrix
		return m;
	if(all (iszerof) (head (m)) == True)
		// first row is empty, skip
		return front (head (m)) (m_rrefWipe (tail (m)));
*/
	let firstrow=head (m);
	let theone=find (isonef) (firstrow);
	let wiped=map (m_rrefWipe_ (firstrow) (theone)) (tail (m));
//	return front (firstrow) (m_rrefWipe (wiped));

	return choose
		(m) // empty matrix
		(choose
			(front (head (m)) (m_rrefWipe (tail (m)))) // first row is empty, skip
			(front (firstrow) (m_rrefWipe (wiped)))
			(all (iszerof) (head (m)))
		)
		(bool_or (isempty(m)) (isempty(head (m))));
}

FUN(m_rrowEchelon,T m){
	return reverse (m_rrefWipe (reverse (m_rowEchelon (m))));
}

FUN(m_inv,T m){
	let l=length (m);
	let m_id = m_identity_f (l);
	let mm = zipWith (concat2) (m) (m_id);
	let e=m_rrowEchelon (mm);
	return map (drop (l)) (e);
}

////////////////////////////////////
// I/O

#define printf_log(fmt...)														\
	do{																			\
		if(lambda::Config::enable_vcd_stdout && lambda::Config::enable_vcd){	\
			char *m_=NULL;														\
			if(asprintf(&m_,fmt)!=-1){											\
				fputs(m_,stdout);												\
				worker_dump_stdout(m_);											\
				free(m_);														\
			}																	\
		}else																	\
			printf(fmt);														\
	}while(0)

typedef Constant<String> StringT;

FUN(printstr,T s){
	if(isReducable (s))
		return eagerApply (printstr) (s);
	else if(isFunction (s))
		return compose (printstr) (s);
	else if(&s!=&nothing)
		printf_log("%s",as<String>(s).c_str());
	return nothing;
}

FUN(hello){
	return printstr ("Hello World!!1\n");
}

FUN(debug,T t){
	LAMBDA_PRINT(code,"debug: %s",t.name().c_str());
	return t;
}

FUN(input,void){
	int i;
	do{
		printf_log("Input? ");
		fflush(NULL);
	}while(!scanf("%d",&i));
	return *new Constant<>(i);
}

static void print_val(Term& t,const char* fmt_other,const char* fmt_int=NULL,const char* fmt_float=NULL,const char* fmt_complex=NULL,const char* fmt_mpz=NULL,const char* fmt_str=NULL){
	Term_ref c=t.FullReduce();
	switch(c.term().GetType()){
	case Term::type_int:
		if(fmt_int){
			printf_log(fmt_int,as<lcint_t>(c));
			break;
		}else
			goto print_as_other;
	case Term::type_float:
		if(fmt_float){
			printf_log(fmt_float,(double)as<lcfloat_t>(c));
			break;
		}else
			goto print_as_other;
	case Term::type_complex:
		if(fmt_complex){
			lccomplex_t z=as<lccomplex_t>(c);
			printf_log(fmt_complex,creal(z),cimag(z));
			break;
		}else
			goto print_as_other;
#ifdef HAVE_GMP
	case Term::type_mpz:
		if(fmt_mpz){
			const lcmpz_t &z=as<lcmpz_t>(c);
			char* c=NULL;
			gmp_asprintf(&c,fmt_mpz,z);
			LAMBDA_ASSERT(c!=NULL,"gmp_asprintf() should not return NULL");
			printf_log("%s",c);
			void (*freefunc)(void*,size_t);
			mp_get_memory_functions(NULL,NULL,&freefunc);
			freefunc(c,strlen(c)+1);
			break;
		}else
			goto print_as_other;
#endif
	case Term::type_string:
		if(fmt_str){
			printf_log(fmt_str,as<String>(c).c_str());
			break;
		}else
			goto print_as_other;
	default:
print_as_other:
		printf_log(fmt_other,&c);
	}
}

FUN(show,T x){
	if(isReducable (x))
		return eagerApply (show) (x);
	else switch(x.GetType()){
		case Term::type_int: return *new StringT(String("%ld",(long)as<lcint_t>(x)));
		case Term::type_float: return *new StringT(String("%f",as<lcfloat_t>(x)));
		case Term::type_complex: return *new StringT(String("%f+%fi",creal(as<lccomplex_t>(x)),cimag(as<lccomplex_t>(x))));
#ifdef HAVE_GMP
		case Term::type_mpz:{
			char* c=NULL;
			gmp_asprintf(&c,"%Zd",as<lcmpz_t>(x));
			LAMBDA_ASSERT(c!=NULL,"gmp_asprintf() should not return NULL");
			StringT* s=new StringT(String(true,c));
			void (*freefunc)(void*,size_t);
			mp_get_memory_functions(NULL,NULL,&freefunc);
			freefunc(c,strlen(c)+1);
			return *s;}
#endif
		case Term::type_string:
			return x;
		default:
			return *new StringT(String(true,"??"));
	}
}

FUN(charlist,T length,T ix,T str){
	return
		choose
		(front ((lcint_t)(as<String>(str).c_str()[as<lcint_t>(ix)])) (charlist (length) (inc (ix)) (str)))
		(end)
		(lt (ix) (length));
}

FUN(string2chars,T str){
	if(isReducable (str))
		return eagerApply (string2chars) (str);
	else
		return charlist ((lcint_t)strlen(as<String>(str).c_str())) (zero) (str);
}

static void print_val(Term& t){
	print_val(t,"?? ","%3d ","%6.2f ","%.2f+%.2fi ","%Zd ","%s ");
}

FUN(printval,T x){
	if(isReducable (x))
		return eagerApply (printval) (x);
	else
		return print_val(x),nothing;
}

FUN(printtailend,T dummy){
	printf_log("]\n");
	return nothing;
}

FUN(printtail,T list){
	LAMBDA_PRINT(misc,"printtail %p ?= %p",&list,&end);
	return choose
		(printtailend (nothing))
		((printval (head (list)),printtail (tail (list))))
		(isempty (list));
}

FUN(printlist,T list){
	LAMBDA_PRINT(misc,"printlist");
	printf_log("list %p = [ ",&list);
	return printtail (list);
}

FUN(printmatrixstart,T dummy){
	printf_log("    [ ");
	return nothing;
}

FUN(printrestmatrix,T matrix){
	return choose
		(nothing)
		((printmatrixstart (nothing), printtail (head (matrix)), printrestmatrix (tail (matrix))))
		(isempty(matrix));
}

FUN(printmatrix,T matrix){
	printf_log("matrix %p =\n",&matrix);
	return printrestmatrix (matrix);
}

FUN(printtuple,T tup){
	return printstr (" ( "), printval (fst (tup)), printval (snd (tup)), printstr(" )");
}

FUN(printresttuples,T ts){
	return choose
		(printtailend (nothing))
		((printtuple (head (ts)),printresttuples (tail (ts))))
		(isempty (ts));
}

FUN(printtuples,T ts){
	printf_log("list %p = [ ",&ts);
	return printresttuples(ts);
}

FUN(print,T t){
	if(t.IsReducable())
		return eagerApply (print) (t);
	else{
		printf_log("term %p = ",&t);
		return print_val(t,"??\n","%d\n","%f\n","%f + %f i\n","%Zd\n","%s\n"),nothing;
	}
}

Term_tref convargs(int argc,char** argv){
	Term_ptr a=&end;
	for(int i;argc>0;argc--)
		a=&(front (sscanf(argv[argc-1],"%i",&i)==1?(lcint_t)i:0) (*a));
	return eagerList (*a);
}

static void print_config(){
#ifndef LAMBDA_BENCHMARK
	LAMBDA_PRINT(always,"Lambda configuration:"
		"\n\tplatform: "
#if defined(LAMBDA_PLATFORM_x86)
		"x86"
#elif defined(LAMBDA_PLATFORM_MAC)
		"mac"
#elif defined(LAMBDA_PLATFORM_MB)
		"mb"
#else
		"unknown"
#endif
		"\n\tcompiled with g++" __VERSION__ " at "__TIMESTAMP__
		"\n\tflags:"
#ifdef LAMBDA_DEBUG
			" debug"
#endif
#ifdef LAMBDA_MEMCHECK
			" memcheck"
#endif
#ifdef HAVE_GMP
			" gmp"
#endif
		"\n\tconfig: w=%d mb=%luKiB ggc=%dms%s%s%s%s%s%s%s",
			Config::workers,
			Config::macroblock_size/1024,
			Config::global_gc_interval_ms,
			Config::enable_stats?" stats":"",
			Config::enable_assert?" assert":"",
			Config::enable_dot?" dot":"",
			Config::enable_vcd?" vcd":"",
			Config::term_queue_atomic?" atomic_q":"",
			Config::atomic_indir?" atomic_indir":"",
			Config::interrupt_sleep?" intr":""
		);
#endif
}

}; // namespace

MAIN_DECL(lambda::T args)

int main(int argc,char** argv){
	using namespace lambda;
	lambda::print_config();
	LAMBDA_PRINT(state,"booting...");
#ifdef HAVE_GMP
	mp_set_memory_functions(noterm_alloc,noterm_realloc_s,noterm_free_s);
	globalize_flushall(); // don't know where the functions above will be stored
#endif
	platform_init();

	let stack_end=end;
	let a=lambda::convargs(argc-1,argv+1);
	let m=lc_main(a);
	dot_timed(m);
	current_worker->Barrier();
	
	platform_start();
	int res=(int)current_worker->Compute(m);
	if(res)
		LAMBDA_PRINT(code,"main = %d",res);
	platform_end();
	
	Stats<>::Print();
	current_worker->Cleanup();
	platform_exit();
	LAMBDA_PRINT(state,"shutdown");
	return res;
}

#endif // __LAMBDA_LIB_H
