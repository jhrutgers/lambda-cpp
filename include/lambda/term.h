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

#ifndef __LAMBDA_TERM_H
#define __LAMBDA_TERM_H

////////////////////////////////////
////////////////////////////////////
// Computational model
////////////////////////////////////
////////////////////////////////////

#include <unistd.h>
#include <typeinfo>

#include <lambda/debug.h>
#include <lambda/stats.h>
#include <lambda/ptr.h>
#include <lambda/stack.h>

namespace lambda {
	template <typename T> class Global;
	class Term;
	struct EvalTerm;

	// implemented in lambda/worker.h
	template <typename T> static void* term_alloc(size_t s);
	static void term_free(void* p);
	static bool worker_halt();
	static void worker_sleep(useconds_t* sleep=NULL);
	static Stack<EvalTerm>& worker_eval_stack();
	static void dot_dump(Term& t,Term& mark);

	////////////////////////////////////
	// Fundamental

	struct EvalTerm {
		typedef enum { eval_stop, eval_catch, eval_normal, eval_stressed, eval_forced, eval_halt } eval_mode_t;
		EvalTerm() {}
		EvalTerm(Term* term,eval_mode_t mode=eval_normal) : term(term), mode(mode) {}
		operator Term*(){return term;}
		static const char* modeName(eval_mode_t mode){
			switch(mode){
			case eval_stop:		return "stop";
			case eval_normal:	return "normal";
			case eval_stressed:	return "stressed";
			case eval_forced:	return "forced";
			case eval_catch:	return "catch";
			case eval_halt:		return "halt";
			default:			return "unknown";
			}
		}
		String name() const;
		Term* term;
		eval_mode_t mode;
	};

	EvalTerm::eval_mode_t& worker_eval_mode(){
		EvalTerm* t=worker_eval_stack().peek();
		LAMBDA_ASSERT(t!=NULL,"querying mode of void");
		return t->mode;
	}

	void worker_eval_setmode(EvalTerm::eval_mode_t mode){
		worker_eval_mode()=mode;
	}

	class Term {
	public:
		enum type_t { type_int, type_float, type_complex, type_mpz, type_string, type_constant, type_function, type_unknown };
		// construction
		Term(bool birth=true) : m_marked(0) {if(birth)MarkBirth();}
		Term(const Term& t,bool birth=true) : m_marked(0) {if(birth)MarkBirth();}
		virtual ~Term(){
#ifdef LAMBDA_DEBUG
			MarkDead();
#endif
		}
//		Term_tref operator()(lcint_t c);
		Term_tref operator()(int c);
		Term_tref operator()(long c);
		Term_tref operator()(long long c);
		Term_tref operator()(lcfloat_t c);
		Term_tref operator()(lccomplex_t c);
		Term_tref operator()(const char* c);
		Term_tref operator()(Term& a){return Apply(a);}
		Term_tref operator()(const Term_tref& r){return Apply(r);}
		Term_tref operator()(const Term_ref& r){return Apply(r);}
		operator Term_tref(){return Term_tref(*this);}
		operator Term_ref(){return Term_ref(*this);}
		virtual Term_tref Apply(Term& a){Error("Cannot apply term %s to non-function %s",a.name().c_str(),this->name().c_str()); }
		virtual String name(int depth=0){
			LAMBDA_ASSERT(!IsDead(),"accessing dead %p",this);
			return String("%s%cerm%s@%p",!IsBorn()?"unborn ":"",IsGlobal()?'T':'t',IsActive()?"!":"",this);}
		virtual type_t GetType(){return type_unknown;}
		virtual bool IsIndirectable(){return false;}
		virtual Term* SetIndirection(Term* ind){return ind;}
		// primitive operators
		virtual Term_tref operator +(Term& rhs){Error("Cannot apply primitive operator + to term %s", name().c_str()); }
		virtual Term_tref operator -(Term& rhs){Error("Cannot apply primitive operator - to term %s", name().c_str()); }
		virtual Term_tref operator *(Term& rhs){Error("Cannot apply primitive operator * to term %s", name().c_str()); }
		virtual Term_tref operator /(Term& rhs){Error("Cannot apply primitive operator / to term %s", name().c_str()); }
		virtual Term_tref operator %(Term& rhs){Error("Cannot apply primitive operator %% to term %s",name().c_str()); }
		virtual bool      operator==(Term& rhs){Error("Cannot apply primitive operator == to term %s",name().c_str()); }
		virtual bool      operator!=(Term& rhs){Error("Cannot apply primitive operator != to term %s",name().c_str()); }
		virtual bool      operator >(Term& rhs){Error("Cannot apply primitive operator > to term %s", name().c_str()); }
		virtual bool      operator <(Term& rhs){Error("Cannot apply primitive operator < to term %s", name().c_str()); }
		virtual bool      operator>=(Term& rhs){Error("Cannot apply primitive operator >= to term %s",name().c_str()); }
		virtual bool      operator<=(Term& rhs){Error("Cannot apply primitive operator <= to term %s",name().c_str()); }
		// reduction
		virtual int Arguments() {return 0;}
		virtual Term& BaseFunction() { return *this; }
		virtual bool IsReducable(){return false;}
		virtual Term_tref Reduce() {return *this;}
		virtual Term_tref ReduceApply(Term* a1=NULL,Term* a2=NULL,Term* a3=NULL,Term* a4=NULL,Term* a5=NULL){return *this;}
		Term_tref FullReduce(EvalTerm::eval_mode_t mode=EvalTerm::eval_normal) {
			Stack<EvalTerm>& stack=worker_eval_stack();
			LAMBDA_ASSERT(!IsDead(),"accessing dead %p",this);
			LAMBDA_PRINT(eval,"doing full reduction of %s",name().c_str());

			EvalTerm* stack_top=stack.top();
			stack.push(EvalTerm(this,mode));
			Term_ptr t;
			Term *r=NULL;

			EvalTerm* top=NULL;
			bool halting=false;
			while((top=stack.top())!=stack_top){
				LAMBDA_ASSERT(top!=NULL,"elements on empty stack");
				r=t=top->term;
				LAMBDA_ASSERT(t!=NULL,"no elements on non-empty stack");
				LAMBDA_PRINT(eval,"stack top: %s",top->name().c_str());

				if(halting)
					switch(top->mode){
					case EvalTerm::eval_catch:
						break;
					default:
						top->mode=EvalTerm::eval_halt;
					}

				switch(top->mode){
				case EvalTerm::eval_stop:
					LAMBDA_PRINT(eval,"term popped itself during reduction");
					stack.pop();
					break;
				case EvalTerm::eval_catch:
				case EvalTerm::eval_normal:
					// normal mode will allow lazy to set the mode to eval_stop
				case EvalTerm::eval_stressed:
					if(t->ReduceWillBlock()){
						LAMBDA_PRINT(eval,"%s might block on reduce, pop from eval stack",r->name().c_str());
						stack.pop();
						break;
					}
				case EvalTerm::eval_forced:
					top->term=r=&t->Reduce();
					LAMBDA_VALIDATE_TERM(*r);
					if(r!=t.ptr()){
						LAMBDA_PRINT(eval,"term %s reduced to %s",t->name(-1).c_str(),r->name().c_str());
						if(Config::dot_all)
							dot_dump(*this,*r);
					}else if(top==stack.top()){
						if(unlikely(top->mode==EvalTerm::eval_halt)){
							LAMBDA_PRINT(eval,"term %s got halted during reduction",t->name().c_str());
							// next iteration will continue to eval_halt case
						}else{
							LAMBDA_PRINT(eval,"cannot reduce %s any further",t->name().c_str());
							stack.pop();
						}
					}else
						LAMBDA_PRINT(eval,"reduction of %s only pushed terms on eval stack",t->name().c_str());
					break;
				case EvalTerm::eval_halt:
					LAMBDA_PRINT(eval,"halting %s",top->term->name().c_str());
					stack.pop();
					halting=true;
					break;
				default:
					LAMBDA_ASSERT(false,"uncovered eval state");
				}
			}

			LAMBDA_ASSERT(r!=NULL,"reduction of %s to NULL",name().c_str());
			LAMBDA_PRINT(eval,"reduction of %s done",name().c_str());
			return *r;
		}
		virtual bool IsBlocked(){return false;}
		virtual bool ReduceWillBlock(){return IsBlocked();}
		virtual bool ReduceApplyWillBlock(){return IsBlocked();}
		Term& TryReduce(Term& when_blocked){return ReduceWillBlock()?when_blocked:*this;}
		// reduction till constant = computation
		virtual const void* Compute() {
			LAMBDA_ASSERT(!IsDead(),"accessing dead %p",this);
			Term& t=FullReduce(EvalTerm::eval_forced);
			if(&t==this)
				Error("Cannot reduce %s any further, not computable",name().c_str());
			return t.Compute();
		}
		template <typename T> const T& Compute(){
			LAMBDA_PRINT(eval,"compute %s to %s",name().c_str(),typeid(T).name());
			return *reinterpret_cast<const T*>(Compute());}
		// memory management
		virtual Term_tref Globalize(Stack<EvalTerm>& stack)=0;
		Term_tref Globalize() {
			Stack<EvalTerm>& stack=worker_eval_stack();
			EvalTerm* stack_top=stack.top();
			LAMBDA_ASSERT(stack_top!=NULL,"globalizing irrelevant term");
			stack.push(this);
			Term_ptr t;
			Term *g=NULL;

			while(stack.top()!=stack_top){
				t=stack.pop().term;
				LAMBDA_ASSERT(t.ptr()!=NULL,"nothing on non-empty stack");
				g=&t->Globalize(stack);
			}

			LAMBDA_ASSERT(g!=NULL,"globalized %s to NULL",name().c_str());
			LAMBDA_ASSERT(g->IsGlobal(),"globalized %s to non-global %s",name().c_str(),g->name().c_str());
			return *g;
		}
		virtual bool IsGlobal(){return false;}
		virtual Term_tref Duplicate(){return *this;}
		Term_tptr MatchMem(Term* t,bool make_global=false){return t?&MatchMem(*t,make_global):NULL;}
		Term_tref MatchMem(Term& t,bool make_global=false){return make_global||IsGlobal()?t.Globalize():Term_tref(t);}
		Term_tptr MatchMemCtor(Term* t,bool make_global=false,Term* saveme=NULL){Term_ptr p=this,s=saveme; return MatchMem(t,make_global);}
		Term_tref MatchMemCtor(Term& t,bool make_global=false,Term* saveme=NULL){Term_ptr p=this,s=saveme; return MatchMem(t,make_global);}
		static void operator delete(void* p){ term_free(p);	}
		virtual Term& FollowIndirection(){ return *this;}
		Term& FollowFullIndirection(){
			// follow chain of indirections without a recursive call to FollowIndirection()
			Term *ind1=NULL,*ind2=this;
			do{
				LAMBDA_ASSERT(ind2!=NULL,"%s indirects to NULL",ind1->name().c_str());
				ind1=ind2;
				ind2=&ind1->FollowIndirection();
			}while(ind1!=ind2);
			return *ind1;
		}

		enum life_t { unborn=0, active=1, old=2, dead=3 };
		bool IsBorn(){return m_marked>unborn;}
		virtual bool IsActive(){return m_marked==active;}
		virtual bool NeedMarking(){return m_marked!=active;}
		bool IsAlive(){return IsActive()||m_marked<=active;}
		bool IsOld(){return !IsActive()&&m_marked==old;}
		bool IsDead(){return m_marked==dead;}

		virtual void MarkActive(Stack<Term*>& more_active){
			LAMBDA_ASSERT(!IsDead(),"accessing dead %s",name().c_str());
			LAMBDA_PRINT(gc_details,"mark active: %s",name().c_str());
#ifdef LAMBDA_DEBUG
			worker_check_stack();
#endif
			if(IsBorn())m_marked=active;}
		void MarkOld(){if(IsBorn())m_marked=old;}
		String DotID(){return String("term_%p",this);}
		String DotName(){return name(-1);}
		virtual void DotFollow(Stack<Term*>& s){}
	protected:
		void MarkBirth(){m_marked=old;}
		void MarkDead(){m_marked=dead;}
		static void* operator new(size_t s);
		template <typename T> static void* operator_new_t(size_t s){
			Stats<>::LocalTerm();
			return term_alloc<T>(s);
		}
		virtual void Reconcile(){}
		Term& operator=(Term& rhs); //don't!
	protected:
		int m_marked;
	};
	
	String EvalTerm::name() const { return String("eval (%s) %s",modeName(mode),term->name().c_str()); }
	
	template <typename T=lcint_t>
	class Constant : public Term {
	public:
		Constant(const T& val) : Term(), m_val(val){ LAMBDA_PRINT(vars,"%s",name().c_str()); }
		Constant(const Constant& c) : Term(), m_val(c.m_val){ LAMBDA_PRINT(vars,"%s",name().c_str()); }
		template <typename A> Constant(A val) : Term(), m_val((A)val){ LAMBDA_PRINT(vars,"%s",name().c_str()); }
		Constant(Constant& c,bool make_global=false) : Term(c), m_val(c.m_val) {}
		virtual const void* Compute() { return &m_val; }
		virtual Term_tref Globalize(Stack<EvalTerm>& stack) { return *new Global<Constant>(*this);}
		virtual Term_tref operator+(Term& rhs){return Term::operator+(rhs);}
		virtual Term_tref operator-(Term& rhs){return Term::operator-(rhs);}
		virtual Term_tref operator*(Term& rhs){return Term::operator*(rhs);}
		virtual Term_tref operator/(Term& rhs){return Term::operator/(rhs);}
		virtual Term_tref operator%(Term& rhs){return Term::operator%(rhs);}
		virtual bool      operator==(Term& rhs){return Term::operator==(rhs);}
		virtual bool      operator!=(Term& rhs){return Term::operator!=(rhs);}
		virtual bool      operator >(Term& rhs){return Term::operator>(rhs);}
		virtual bool      operator <(Term& rhs){return Term::operator<(rhs);}
		virtual bool      operator>=(Term& rhs){return Term::operator>=(rhs);}
		virtual bool      operator<=(Term& rhs){return Term::operator<=(rhs);}
		virtual type_t GetType(){return type_constant;}
		static void* operator new(size_t s){return Term::operator_new_t<Constant<T> >(s);}
		virtual String name(int depth=0){return String("%constant<%s>%s@%p",IsGlobal()?'C':'c',typeid(T).name(),this->IsActive()?"!":"",this);}
	protected:
		virtual void Reconcile(){Term::Reconcile();}
	private:
		const T m_val;
	};

	template <> Term::type_t Constant<lcint_t>::GetType(){return type_int;}
	template<> Term_tref Constant<lcint_t>::operator+(Term& rhs){lcint_t v=m_val+rhs.Compute<lcint_t>();return *new Constant(v);}
	template<> Term_tref Constant<lcint_t>::operator-(Term& rhs){lcint_t v=m_val-rhs.Compute<lcint_t>();return *new Constant(v);}
	template<> Term_tref Constant<lcint_t>::operator*(Term& rhs){lcint_t v=m_val*rhs.Compute<lcint_t>();return *new Constant(v);}
	template<> Term_tref Constant<lcint_t>::operator/(Term& rhs){lcint_t v=m_val/rhs.Compute<lcint_t>();return *new Constant(v);}
	template<> Term_tref Constant<lcint_t>::operator%(Term& rhs){lcint_t v=m_val%rhs.Compute<lcint_t>();return *new Constant(v);}
	template<> bool	     Constant<lcint_t>::operator==(Term& rhs){return m_val==rhs.Compute<lcint_t>();}
	template<> bool	     Constant<lcint_t>::operator!=(Term& rhs){return m_val!=rhs.Compute<lcint_t>();}
	template<> bool	     Constant<lcint_t>::operator >(Term& rhs){return m_val >rhs.Compute<lcint_t>();}
	template<> bool	     Constant<lcint_t>::operator <(Term& rhs){return m_val <rhs.Compute<lcint_t>();}
	template<> bool	     Constant<lcint_t>::operator>=(Term& rhs){return m_val>=rhs.Compute<lcint_t>();}
	template<> bool	     Constant<lcint_t>::operator<=(Term& rhs){return m_val<=rhs.Compute<lcint_t>();}
	template <> Term::type_t Constant<lcfloat_t>::GetType(){return type_float;}
	template<> Term_tref Constant<lcfloat_t>::operator+(Term& rhs){lcfloat_t v=m_val+rhs.Compute<lcfloat_t>();return *new Constant(v);}
	template<> Term_tref Constant<lcfloat_t>::operator-(Term& rhs){lcfloat_t v=m_val-rhs.Compute<lcfloat_t>();return *new Constant(v);}
	template<> Term_tref Constant<lcfloat_t>::operator*(Term& rhs){lcfloat_t v=m_val*rhs.Compute<lcfloat_t>();return *new Constant(v);}
	template<> Term_tref Constant<lcfloat_t>::operator/(Term& rhs){lcfloat_t v=m_val/rhs.Compute<lcfloat_t>();return *new Constant(v);}
	template<> bool	     Constant<lcfloat_t>::operator >(Term& rhs){return m_val >rhs.Compute<lcfloat_t>();}
	template<> bool	     Constant<lcfloat_t>::operator <(Term& rhs){return m_val <rhs.Compute<lcfloat_t>();}
	template <> Term::type_t Constant<lccomplex_t>::GetType(){return type_complex;}
	template<> Term_tref Constant<lccomplex_t>::operator+(Term& rhs){lccomplex_t v=m_val+rhs.Compute<lccomplex_t>();return *new Constant(v);}
	template<> Term_tref Constant<lccomplex_t>::operator-(Term& rhs){lccomplex_t v=m_val-rhs.Compute<lccomplex_t>();return *new Constant(v);}
	template<> Term_tref Constant<lccomplex_t>::operator*(Term& rhs){lccomplex_t v=m_val*rhs.Compute<lccomplex_t>();return *new Constant(v);}
	template<> Term_tref Constant<lccomplex_t>::operator/(Term& rhs){lccomplex_t v=m_val/rhs.Compute<lccomplex_t>();return *new Constant(v);}

	template<> String Constant<lcint_t>::name(int depth){return String("%constant(%d)%s@%p",IsGlobal()?'C':'c',(int)m_val,IsActive()?"!":"",this);}
	template<> String Constant<lcfloat_t>::name(int depth){return String("%constant(%.3f)%s@%p",IsGlobal()?'C':'c',(double)m_val,IsActive()?"!":"",this);}
	template<> String Constant<lccomplex_t>::name(int depth){return String("%constant(%.3f+%.3fi)%s@%p",IsGlobal()?'C':'c',creal(m_val),cimag(m_val),IsActive()?"!":"",this);}

//	Term_tref Term::operator()(lcint_t c){return Apply(Term_ref(*new Constant<lcint_t>(c)));}
	Term_tref Term::operator()(int c){return Apply(Term_ref(*new Constant<lcint_t>((lcint_t)c)));}
	Term_tref Term::operator()(long c){return Apply(Term_ref(*new Constant<lcint_t>((lcint_t)c)));}
	Term_tref Term::operator()(long long c){return Apply(Term_ref(*new Constant<lcint_t>((lcint_t)c)));}
	Term_tref Term::operator()(lcfloat_t c){return Apply(Term_ref(*new Constant<lcfloat_t>(c)));}
	Term_tref Term::operator()(lccomplex_t c){return Apply(Term_ref(*new Constant<lccomplex_t>(c)));}
	Term_tref Term::operator()(const char* s){return Apply(Term_ref(*new Constant<String>(String(true,s))));}
	
	template <> Term::type_t Constant<String>::GetType(){return type_string;}
	template<> void Constant<String>::Reconcile(){if(IsGlobal())this->m_val.Reconcile();}

#ifdef HAVE_GMP
	template <> class Constant<lcmpz_t> : public Term {
	public:
		Constant(const lcint_t& val) : Term(false){						mpz_init_set_si(m_val,val);			MarkBirth(); }
		Constant(const char* val) : Term(false){						mpz_init_set_str(m_val,val,0);		MarkBirth(); }
		Constant(const Constant& c) : Term(false){						mpz_init_set(m_val,c.m_val);		MarkBirth(); }
		template <typename A> Constant(A val) : Term(false){			mpz_init_set_si(m_val,(lcint_t)val);MarkBirth(); }
		Constant(Constant& c,bool make_global=false) : Term(c,false) {	mpz_init_set(m_val,c.m_val);		MarkBirth(); }
		virtual ~Constant() { mpz_clear(m_val); }

		virtual const void* Compute() { return &m_val; }
		virtual Term_tref Globalize(Stack<EvalTerm>& stack);
		virtual Term_tref operator+(Term& rhs){
			Constant* c=new Constant(m_val);
			Term_ref save=*c;
			switch(rhs.GetType()){
			case type_int:{
				lcint_t i=rhs.Compute<lcint_t>();
				if(i>=0)mpz_add_ui(c->m_val,m_val,(unsigned long int)i);
					else mpz_sub_ui(c->m_val,m_val,(unsigned long int)-i);
				break;}
			case type_mpz:
				mpz_add(c->m_val,m_val,rhs.Compute<lcmpz_t>());
				break;
			default:
				LAMBDA_ASSERT(false,"invalid mpz type for operation");
			}
			return *c;
		}
		virtual Term_tref operator-(Term& rhs){
			Constant* c=new Constant(m_val);
			Term_ref save=*c;
			switch(rhs.GetType()){
			case type_int:{
				lcint_t i=rhs.Compute<lcint_t>();
				if(i>=0)mpz_sub_ui(c->m_val,m_val,(unsigned long int)i);
					else mpz_add_ui(c->m_val,m_val,(unsigned long int)-i);
				break;}
			case type_mpz:
				mpz_sub(c->m_val,m_val,rhs.Compute<lcmpz_t>());
				break;
			default:
				LAMBDA_ASSERT(false,"invalid mpz type for operation");
			}
			return *c;
		}
		virtual Term_tref operator*(Term& rhs){
			Constant* c=new Constant();
			Term_ref save=*c;
			switch(rhs.GetType()){
			case type_int:	mpz_mul_si(c->m_val,m_val,rhs.Compute<lcint_t>());	break;
			case type_mpz:	mpz_mul(c->m_val,m_val,rhs.Compute<lcmpz_t>());		break;
			default:		LAMBDA_ASSERT(false,"invalid mpz type for operation");
			}
			return *c;
		}
		virtual Term_tref operator/(Term& rhs){
			Constant* c=new Constant(m_val);
			Term_ref save=*c;
			switch(rhs.GetType()){
			case type_int:{
				lcint_t i=rhs.Compute<lcint_t>();
				if(i>=0)
					mpz_tdiv_q_ui(c->m_val,m_val,(unsigned long int)i);
				else{
					mpz_tdiv_q_ui(c->m_val,m_val,(unsigned long int)-i);
					mpz_neg(c->m_val,c->m_val);
				}
				break;}
			case type_mpz:
				mpz_tdiv_q(c->m_val,m_val,rhs.Compute<lcmpz_t>());
				break;
			default:		LAMBDA_ASSERT(false,"invalid mpz type for operation");
			}
			return *c;
		}
		virtual Term_tref operator%(Term& rhs){
			Constant* c=new Constant(m_val);
			Term_ref save=*c;
			switch(rhs.GetType()){
			case type_int:	mpz_mod_ui(c->m_val,m_val,(unsigned long int)rhs.Compute<lcint_t>());	break;
			case type_mpz:	mpz_mod(c->m_val,m_val,rhs.Compute<lcmpz_t>());							break;
			default:		LAMBDA_ASSERT(false,"invalid mpz type for operation");
			}
			return *c;
		}
		virtual bool      operator==(Term& rhs){return mpzcmp(rhs)==0;}
		virtual bool      operator!=(Term& rhs){return mpzcmp(rhs)!=0;}
		virtual bool      operator >(Term& rhs){return mpzcmp(rhs)>0;}
		virtual bool      operator <(Term& rhs){return mpzcmp(rhs)<0;}
		virtual bool      operator>=(Term& rhs){return mpzcmp(rhs)>=0;}
		virtual bool      operator<=(Term& rhs){return mpzcmp(rhs)<=0;}
		virtual type_t GetType(){return type_mpz;}
		static void* operator new(size_t s){return Term::operator_new_t<Constant<lcmpz_t> >(s);}
		virtual String name(int depth=0){
			if(!IsBorn())
				return String("unborn %constant(0x...)@%p",IsGlobal()?'C':'c',this);
			else
				return String("%constant(0x...%lx)%s@%p",IsGlobal()?'C':'c',mpz_get_ui(m_val),IsActive()?"!":"",this);
		}
	protected:
		Constant() : Term(false){mpz_init(m_val); MarkBirth();}
		Constant(const mpz_t& m) : Term(false){mpz_init2(m_val,mpz_size(m)*mp_bits_per_limb); MarkBirth();}
		int mpzcmp(Term& rhs){
			switch(rhs.GetType()){
			case type_int:{
				lcint_t i=rhs.Compute<lcint_t>();
				return mpz_cmp_si(m_val,i);}
			case type_mpz:{
				const lcmpz_t &m=rhs.Compute<lcmpz_t>();
				return mpz_cmp(m_val,m);}
			default:
				LAMBDA_ASSERT(false,"invalid mpz type for operation");
				return 0;
			}
		}
		virtual void Reconcile(){
			if(IsGlobal())
				globalize_flushmem(m_val[0]._mp_d,sizeof(*m_val[0]._mp_d)*m_val[0]._mp_alloc);
		}
	private:
		lcmpz_t m_val;
	};
#endif

	class Function : public Term {
	public:
		typedef Term_tref (*f_type)(Term&,Term&,Term&,Term&,Term&);

		Function(Term_tref (*f)(void),const char* label=NULL)							: Term(), m_f(reinterpret_cast<typeof(m_f)>(f)), m_n(0), m_indirect(NULL), m_label(label) {}
		Function(Term_tref (*f)(Term&),const char* label=NULL)							: Term(), m_f(reinterpret_cast<typeof(m_f)>(f)), m_n(1), m_indirect(NULL), m_label(label) {}
		Function(Term_tref (*f)(Term&,Term&),const char* label=NULL)					: Term(), m_f(reinterpret_cast<typeof(m_f)>(f)), m_n(2), m_indirect(NULL), m_label(label) {}
		Function(Term_tref (*f)(Term&,Term&,Term&),const char* label=NULL)				: Term(), m_f(reinterpret_cast<typeof(m_f)>(f)), m_n(3), m_indirect(NULL), m_label(label) {}
		Function(Term_tref (*f)(Term&,Term&,Term&,Term&),const char* label=NULL)		: Term(), m_f(reinterpret_cast<typeof(m_f)>(f)), m_n(4), m_indirect(NULL), m_label(label) {}
		Function(Term_tref (*f)(Term&,Term&,Term&,Term&,Term&),const char* label=NULL)	: Term(), m_f(reinterpret_cast<typeof(m_f)>(f)), m_n(5), m_indirect(NULL), m_label(label) {}
		
		virtual Term_tref Apply(Term& a);
		virtual Term_tref Reduce() { 
			if(GetIndirection()){
				LAMBDA_VALIDATE_TERM(*GetIndirection());
				return *GetIndirection();
			}else if(Arguments()==0)
				return ReduceApply();
			else
				return *this;
			//return Arguments()==0?ReduceApply():*this;
		}
		virtual int Arguments() { return GetIndirection()?FollowFullIndirection().Arguments():m_n; }
		virtual bool IsReducable(){return Arguments()==0;}
		virtual bool IsIndirectable(){return Arguments()==0;}
		virtual Term* SetIndirection(Term* ind){
			LAMBDA_VALIDATE_TERM(*ind);
			return m_indirect.set(ind),ind;}
		virtual Term& FollowIndirection(){ return *(GetIndirection()?GetIndirection():this);}
		virtual Term_tref ReduceApply(Term* a1=NULL,Term* a2=NULL,Term* a3=NULL,Term* a4=NULL,Term* a5=NULL){
			LAMBDA_PRINT(eval,"applying function %s (%p,%p,%p,%p,%p)",name().c_str(),a1,a2,a3,a4,a5);
			Term_ptr t=NULL;

			switch(Arguments()){
			case 5:if(a5==NULL)goto no_exec;else break;
			case 4:if(a4==NULL)goto no_exec;else break;
			case 3:if(a3==NULL)goto no_exec;else break;
			case 2:if(a2==NULL)goto no_exec;else break;
			case 1:if(a1==NULL)goto no_exec;else break;
			default:break;
			}

			LAMBDA_PRINT(eval,"argument list complete, execute %s",m_label);
			t=ApplyNow(a1,a2,a3,a4,a5);
//			LAMBDA_PRINT(eval,"applying remaining arguments to the result %s",t.ptr()->name().c_str());
			switch(Arguments()){
			case 0: if(a1!=NULL)t=&(*t)(*a1);else return t;
			case 1: if(a2!=NULL)t=&(*t)(*a2);else return t;
			case 2: if(a3!=NULL)t=&(*t)(*a3);else return t;
			case 3: if(a4!=NULL)t=&(*t)(*a4);else return t;
			case 4: if(a5!=NULL)t=&(*t)(*a5);else return t;
			default:
				return t;
			}

		no_exec:
			LAMBDA_PRINT(eval,"argument list incomplete, only apply");
			t=this;
			if(a1!=NULL)t=&(*t)(*a1);else return t;
			if(a2!=NULL)t=&(*t)(*a2);else return t;
			if(a3!=NULL)t=&(*t)(*a3);else return t;
			if(a4!=NULL)t=&(*t)(*a4);else return t;
			if(a5!=NULL)t=&(*t)(*a5);
			return t;
		}
		virtual Term_tref Globalize(Stack<EvalTerm>& stack){return *this;}
		virtual bool IsGlobal(){ return true; }
		virtual bool IsActive(){return true;}
		virtual void MarkActive(Stack<Term*>& more_active){if(GetIndirection())more_active.push(GetIndirection());}
		virtual String name(int depth=0){return String("%s@%p",m_label?m_label:"(anonymous function)",this);}
		virtual type_t GetType(){return type_function;}
		virtual bool IsBlocked(){
			return GetIndirection()&&FollowFullIndirection().IsBlocked();}
		virtual bool ReduceWillBlock(){
			return GetIndirection()&&FollowFullIndirection().ReduceWillBlock();}
		virtual bool ReduceApplyWillBlock(){
			return GetIndirection()&&FollowFullIndirection().ReduceApplyWillBlock();}
		virtual void DotFollow(Stack<Term*>& s){
			if(GetIndirection())
				s.push(&FollowFullIndirection());
		}
	protected:
		virtual void Reconcile(){
			globalize_flushmem(this,sizeof(*this));
		}
		Term* GetIndirection() const {
			return (Term*)m_indirect.raw();
		}
		static void* operator new(size_t);
		static void operator delete(void*){Error("cannot delete a function");}
		virtual f_type GetFunction(){return m_f;}
		virtual Term* ApplyNow(Term* a1,Term* a2,Term* a3,Term* a4,Term* a5){
			if(GetIndirection()){
				Term_ref i=FollowFullIndirection();
				return &i.ReduceApply(a1,a2,a3,a4,a5);
			}else{
				Term_ptr t=&m_f(*a1,*a2,*a3,*a4,*a5);
				if(Arguments()==0)
					SetIndirection(&t->Globalize());
				return t;
			}
		}
	private:
		f_type const m_f;
		int const m_n;
		volatile_t<Term_ptr>::type m_indirect;
		const char* m_label;
	} ATTR_SHARED_ALIGNMENT;

	class Application : public Term {
	public:
		Application(Term& f,Term& a) : Term(), m_f(f), m_a(a), m_indirect(NULL,noflush) {LAMBDA_PRINT(vars,"new apply %s",name().c_str());}
		Application(Application& a,bool make_global=false) : Term(a,false),
			m_f((Term*)a.GetIndirection()?a.m_f:(Term&)MatchMemCtor(a.m_f,make_global)),
			m_a((Term*)a.GetIndirection()?a.m_a:(Term&)MatchMemCtor(a.m_a,make_global,&m_f)),
			m_indirect(MatchMemCtor((Term*)a.GetIndirection(),make_global),noflush) {
			MarkBirth();
		}
		virtual Term_tref Reduce() {
			LAMBDA_PRINT(eval,"reducing %s",name().c_str());
			if(GetIndirection()){
				LAMBDA_VALIDATE_TERM(*GetIndirection());
				return *(SetIndirectionField(&FollowFullIndirection()));//*m_indirect;
			}else if(m_f.Arguments()>1){
				LAMBDA_PRINT(eval,"function %s requires %d arguments, not reducing",m_f.name().c_str(),m_f.Arguments());
				return *this;
			}else if(m_f.Arguments()<=0){
				LAMBDA_PRINT(eval,"function %s requires no arguments, reduce that first",m_f.name().c_str());
				worker_eval_stack().push(&m_f);
				return *this;//*SetIndirection(new Application(m_f,m_a));
			}else{
				Stats<>::Application();
				Term& t=ReduceApply();
				LAMBDA_ASSERT(!IsGlobal()||t.IsGlobal(),"global %s reduced to non-global %s",name().c_str(),t.name().c_str());
				Term& t2=*SetIndirectionWhen(&t,true);
				LAMBDA_VALIDATE_TERM(t2);
				return t2;
			}
		}
		virtual int Arguments() {
			if(GetIndirection())
				return FollowFullIndirection().Arguments();
			else{
				Term *f1,*f2=&BaseFunction();
				int appl=0;
				do{
					f1=f2;
					f2=&f1->BaseFunction();
					appl++;
				}while(f1!=f2);
				return f2->Arguments()-appl;
			}
		}
		virtual bool IsBlocked(){
			return GetIndirection()&&FollowFullIndirection().IsBlocked();}
		virtual bool ReduceWillBlock(){
			return GetIndirection()&&FollowFullIndirection().ReduceWillBlock();}
		virtual bool ReduceApplyWillBlock(){
			return GetIndirection()?FollowFullIndirection().ReduceApplyWillBlock():m_a.IsBlocked()||m_f.IsBlocked();}
		virtual Term& BaseFunction(){return GetIndirection()?FollowFullIndirection().BaseFunction():m_f;}
		Term& GetArgument(){return m_a;}
		virtual bool IsReducable(){return GetIndirection()||m_f.Arguments()<=1;}
		virtual bool IsIndirectable(){return true;}
		virtual Term_tref ReduceApply(Term* a1=NULL,Term* a2=NULL,Term* a3=NULL,Term* a4=NULL,Term* a5=NULL){
			if(GetIndirection()){
				LAMBDA_ASSERT(!IsGlobal()||GetIndirection()->IsGlobal(),"indirection of global %p to non-global %p",this,GetIndirection());
				Term_ref i=FollowFullIndirection();
				return i.ReduceApply(a1,a2,a3,a4,a5);
			}else{
//				LAMBDA_PRINT(eval,"apply %s (%p,%p,%p,%p,%p)",name().c_str(),a1,a2,a3,a4,a5);
				LAMBDA_ASSERT(!IsGlobal()||(m_f.IsGlobal()&&m_a.IsGlobal()),"application of global %p by non-global %p/%p",this,&m_f,&m_a);
				Term_ref f=m_f.ReduceApply(&m_a,a1,a2,a3,a4);
				return a5?*new Application(f,*a5):f;
			}
		}
		virtual Term_tref Apply(Term& a){
			LAMBDA_VALIDATE_TERM(a,"applied to %s",name().c_str());
			LAMBDA_PRINT(prog,"function application %s (%s)",name().c_str(),a.name().c_str());
			if(GetIndirection())
				return FollowFullIndirection()(a);
			else
				return *new Application(*this,a);
		}
		virtual Term_tref Globalize(Stack<EvalTerm>& stack);
		static void* operator new(size_t s){return Term::operator_new_t<Application>(s);}
		virtual Term& FollowIndirection(){return *(GetIndirection()?GetIndirection():this);}
		virtual void MarkActive(Stack<Term*>& more_active){
			if(!IsBorn()){
				// still constructing...
				LAMBDA_PRINT(gc_details,"%s not marking active",name().c_str());
			}else if(!NeedMarking()){
				// already alive
				LAMBDA_ASSERT(IsGlobal()||!GetIndirection()||GetIndirection()->IsAlive(),"alive %s indirects to dead %s",name().c_str(),GetIndirection()->name().c_str());
				LAMBDA_ASSERT(IsGlobal()||GetIndirection()||m_f.IsAlive(),"alive %s applies to dead %s",name().c_str(),m_f.name().c_str());
				LAMBDA_ASSERT(IsGlobal()||GetIndirection()||m_a.IsAlive(),"alive %s applies dead %s",name().c_str(),m_a.name().c_str());
			}else{
				// recursive marking
				if(GetIndirection()){
					LAMBDA_ASSERT(!IsGlobal()||GetIndirection()->IsGlobal(),"global %s indirects to non-global %s",name().c_str(),GetIndirection()->name().c_str());
					more_active.push(SetIndirectionField(&FollowFullIndirection()));
				}else{
					LAMBDA_ASSERT(!IsGlobal()||m_f.IsGlobal(),"global %s pointing to non-global function %p (%s)",name().c_str(),&m_f,typeid(m_f).name());
					LAMBDA_ASSERT(!IsGlobal()||m_a.IsGlobal(),"global %s pointing to non-global argument %p (%s)",name().c_str(),&m_a,typeid(m_a).name());
					more_active.push(&m_f);
					more_active.push(&m_a);
				}
				Term::MarkActive(more_active);
			}
		}
		virtual String name(int depth=0){
			if(!IsBorn())
				return String("unborn %cpply@%p",IsGlobal()?'A':'a',this);
			else if(depth==-1)
				return String("%cpply%s@%p",IsGlobal()?'A':'a',IsActive()?"!":"",this);
			else if(depth>Config::max_name_depth)
				return String("%cpply%s@%p ...(truncated)",IsGlobal()?'A':'a',IsActive()?"!":"",this);
			else if(GetIndirection()){
				LAMBDA_VALIDATE_TERM(*GetIndirection());
				return String("%cpply%s@%p -> %s",IsGlobal()?'A':'a',IsActive()?"!":"",this,GetIndirection()->name(depth+1).c_str());
			}else{
				LAMBDA_VALIDATE_TERM(m_f);
				LAMBDA_VALIDATE_TERM(m_a);
				char indent_fmt[9];
				sprintf(indent_fmt,"    %%%ds",depth*2>98?98:depth*2);
				char indent[100];
				snprintf(indent,sizeof(indent),indent_fmt,"");
				return String("%cpply%s@%p(\n%s%s,\n%s%s)",IsGlobal()?'A':'a',IsActive()?"!":"",this,indent,m_f.name(depth+1).c_str(),indent,m_a.name(depth+1).c_str());
			}
		}
		virtual type_t GetType(){return GetIndirection()?FollowFullIndirection().GetType():type_function;}
		virtual void DotFollow(Stack<Term*>& s){
			if(GetIndirection()){
				s.push(&FollowFullIndirection());
			}else{
				s.push(&m_f);
				s.push(&m_a);
			}
		}
		virtual Term* SetIndirection(Term* ind){
			return SetIndirectionWhen(ind,false);
		}
protected:
		Term* SetIndirectionWhen(Term* ind,bool only_when_null=false){
			LAMBDA_VALIDATE_TERM(*ind);
			if(ind!=this){
				if(Config::enable_stats&&GetIndirection()&&IsGlobal()){
					LAMBDA_PRINT(eval,"did work for %s twice: second time is %s",name().c_str(),ind->name().c_str());
					Stats<>::Double();
					ind=GetIndirectionField();
				}else{
					LAMBDA_PRINT(vars,"indirection of %s to %s",name().c_str(),ind->name().c_str());
					if(only_when_null&&Config::atomic_indir){
						Term* res=SetIndirectionFieldWhen(ind,NULL);
						if(res!=NULL)ind=res;
					}else
						SetIndirectionField(ind);
				}
			}
			return ind;
		}
		Term* GetIndirection() const {
			return GetIndirectionField();
		}
	protected:
		virtual Term* SetIndirectionField(Term* t){m_indirect.raw()=t; return t;}
		virtual Term* SetIndirectionFieldWhen(Term* t,Term* old){m_indirect.raw()=t; return old;}
		Term* GetIndirectionField() const {return m_indirect.raw();}
		Term* SetIndirectionVolatileField(Term* t){m_indirect=t; return t;}
		Term* SetIndirectionVolatileFieldWhen(Term* t,Term* old){return m_indirect.set_when(t,old);}
	private:
		Term& m_f;
		Term& m_a;
		volatile_t<Term*>::type m_indirect;
	};

	Term_tref Function::Apply(Term& a){
		LAMBDA_VALIDATE_TERM(a,"applied to %s",name().c_str());
		LAMBDA_PRINT(prog,"function application %s (%s)",name().c_str(),a.name().c_str());
		return *new Application(*this,a);}

	
	////////////////////////////////////
	// Global wrappers

	template <typename T>
	class Global : public T {
	public:
		typedef T base;
		Global(T& t) : base(t,true) {
			LAMBDA_PRINT(globals,"%s is globalized copy of %s",this->name().c_str(),t.name().c_str());
			Reconcile();
		}
		Global(Global& g) : base((T&)g,true) {
			LAMBDA_PRINT(globals,"%s is duplicate of %s",this->name().c_str(),g.name().c_str());
			Reconcile();
		}
//		template <typename A1> Global(A1 a1) : base(a1) { Reconcile(); }
		template <typename A1,typename A2> Global(A1& a1,A2& a2) : base(a1,a2) { Reconcile(); }
		template <typename A1> Global(const A1& a1) : base(a1) { Reconcile(); }
		template <typename A1> Global(A1* a1) : base(a1) { Reconcile(); }
		virtual Term_tref Reduce() { return base::Reduce(); }
///		virtual Term_tref Reduce() {
///			entry_x(); Term_ref t=base::Reduce();
///			LAMBDA_VALIDATE_TERM(t);
///			exit_x(); return t; }
///		virtual const void* Compute() {
///			entry_x(); const void* res=base::Compute(); exit_x(); return res; }
///		virtual Term_tref Apply(Term& a) {
///			LAMBDA_VALIDATE_TERM(a);
///			entry_x(); Term_ref t=base::Apply(a); exit_x();
/////			Term_ref res=t.term().Globalize();
/////			LAMBDA_ASSERT(&res!=&t||t.term().IsGlobal(),"globalized %p to non-global",t.ptr());
/////			LAMBDA_PRINT(prog,"applying %p to global %p = %p, globalized to %p\n",&a,this,t.ptr(),res.ptr());
///			return t;//res;
///		}
///		virtual int Arguments() {
///			entry_x(); int n=base::Arguments(); exit_x(); return n;}
///		virtual Term_tref ReduceApply(Term* a1=NULL,Term* a2=NULL,Term* a3=NULL,Term* a4=NULL,Term* a5=NULL){
///			entry_x(); Term_ref t=base::ReduceApply(a1,a2,a3,a4,a5); exit_x();
///			return /*this->MatchMem((Term&)t)*/ t; }
		virtual Term_tref Globalize(Stack<EvalTerm>& stack) { return *this; }
		virtual bool IsGlobal(){ return true; }
		virtual Term_tref Duplicate() { return base::Duplicate(); }
		static void* operator new(size_t s){
			Stats<>::GlobalizedTerm();
			void* p=Term::operator_new_t<Global<T> >(s);
//			entry_x(*(Global*)p); //matching exit_x() in constructor
			return p;
		}
	protected:
		virtual void Reconcile(){
			base::Reconcile();
			globalize_flushmem(this,sizeof(*this));
		}
//		void entry_x(){entry_x(*this);} // entry is usually called after reading the object's type, but before reading its data!
//		static void entry_x(Global& that){}
//		void exit_x(){LAMBDA_PRINT(misc,"flushing %p",this);}
		Term* SetIndirectionField(Term* t){return base::SetIndirectionVolatileField(t);}
		Term* SetIndirectionFieldWhen(Term* t,Term* old){return base::SetIndirectionVolatileFieldWhen(t,old);}
		Term* GetIndirectionField(){return base::GetIndirectionVolatileField();}
	} ATTR_SHARED_ALIGNMENT;

	Term_tref Application::Globalize(Stack<EvalTerm>& stack) {
		if(GetIndirection()){
			Term_ref i=FollowFullIndirection();
			return i.term().Globalize(stack);
		}else{
			Term& gf=m_f.FollowFullIndirection();
			Term& ga=m_a.FollowFullIndirection();
			bool bf=gf.IsGlobal()||!gf.IsIndirectable(),ba=ga.IsGlobal()||!ga.IsIndirectable();
			if(!bf||!ba)
				stack.push(this);
			if(!bf)
				stack.push(&gf);
			if(!ba)
				stack.push(&ga);
			if(!bf||!ba)
				return *this;
			else
				return *SetIndirection(new Global<Application>(*this));
		}
	}

#ifdef HAVE_GMP
	Term_tref Constant<lcmpz_t>::Globalize(Stack<EvalTerm>& stack) { return *new Global<Constant>(*this);}
#endif

	template <> Term_tref Global<Application>::Duplicate() {
		LAMBDA_PRINT(globals,"making local copy of %s",this->name().c_str());
		Term& t=*new Application(*this);
		LAMBDA_PRINT(vars,"%s is a local copy of %s",t.name().c_str(),this->name().c_str());
		return t;
	}
	
	template <typename T>
	class Static : public Global<T> {
	public:
		typedef Global<T> base;
//		Static() : base() {Reconcile();}
//		template <typename A1> Static(A1 a1) : base(a1) {}
		template <typename A1,typename A2> Static(A1& a1,A2& a2) : base(a1,a2) {}
		template <typename A1> Static(const A1& a1) : base(a1) {}
		template <typename A1> Static(A1* a1) : base(a1) {}
//		virtual Term_tref Globalize(Stack<EvalTerm>& stack) { return *this; }
		virtual bool IsGlobal(){ return true; }
//		virtual Term_tref Duplicate() { return *this; }
		virtual bool IsActive(){return true;}
		virtual bool NeedMarking(){return true;}
//		virtual void MarkActive(Stack<Term*>& more_active){}
	protected:
		Static(const Static& s);
		static void* operator new(size_t);
		static void operator delete(void*){Error("cannot delete a static term");}
//		virtual void Reconcile(){
//			base::Reconcile();
//			globalize_flushmem(this,sizeof(*this));
//		}
	} ATTR_SHARED_ALIGNMENT;
	
	
	////////////////////////////////////
	// Efficiency wrappers
	
	extern Function blackhole_finisher;

	class Blackhole : public Term {
	public:
		enum state_t { noresult='?', calculating='*', done='!' };

		Blackhole(Term& t) : Term(), m_result(NULL,noflush), m_t(t) {LAMBDA_PRINT(vars,"new %s",name().c_str());}
		Blackhole(Blackhole& b,bool make_global=false) : Term(b,false),
			m_result(b.GetResult(),noflush),
			m_t(m_result.raw()?Term_tref(b.m_t):MatchMemCtor(b.m_t,make_global,m_result.raw())) {
			MarkBirth();
		}
		virtual Term_tref Finish(Term& t){
			LAMBDA_ASSERT(GetState()!=noresult,"finishing non-calculating %s",name().c_str());
			LAMBDA_PRINT(eval,"finishing %s with %s",name().c_str(),t.name().c_str());
			Term_ptr p;
			if(GetState(&p)==done)
				return *p;
			else{
				Term_ref r=MatchMem(t.FollowFullIndirection());
				return *SetState(done,&r);
			}
		}
		virtual Term_tref Reduce() {
			switch(GetState()){
			case noresult:
				if(SetState(calculating)==NULL){
					Term_ref t=m_t.Duplicate();
					LAMBDA_VALIDATE_TERM(t);
					LAMBDA_PRINT(eval,"eager reduction of %s via %s",name().c_str(),t.term().name().c_str());
					return blackhole_finisher(*this)(t);
				}else
					LAMBDA_PRINT(worker,"preventing concurrent calculation of %s",name().c_str());
			case calculating:
				LAMBDA_PRINT(par,"stalling on %s...",name().c_str());
				Stats<>::Stall();
			case done:
			default:{
				Term_ptr result;
				useconds_t sleep=Config::worker_idle_sleep_min;
				while(GetState(&result)!=done){
					worker_sleep(&sleep);
					if(worker_halt())
						return *this;
				}
				LAMBDA_VALIDATE_TERM(*result);
				return *result;}
			}
		}
		virtual bool IsBlocked(){
			switch(GetState()){
			case calculating:	return true;
			default:			return false;
			}
		}
		virtual bool ReduceWillBlock(){
			Term_ptr res;
			switch(GetState(&res)){
			case noresult:		return m_t.ReduceApplyWillBlock();
			case done:			return res->ReduceApplyWillBlock();
			default:			return true;
			}
		}
		virtual bool ReduceApplyWillBlock(){
			return IsBlocked();
		}
		virtual Term_tref Apply(Term& a){
			LAMBDA_VALIDATE_TERM(a);
			LAMBDA_PRINT(prog,"function application %s (%s)",name().c_str(),a.name().c_str());
			return *new Application(*this,a);}
		virtual int Arguments() {
			Term_ptr result;
			return GetState(&result)==done?result->Arguments():m_t.Arguments();
		}
		virtual bool IsReducable(){return true;}
		virtual bool IsIndirectable(){return true;}
		virtual Term_tref ReduceApply(Term* a1=NULL,Term* a2=NULL,Term* a3=NULL,Term* a4=NULL,Term* a5=NULL){
//			LAMBDA_PRINT(code,"blackholing a partial function is (probably) inefficient; applying arguments to %s",name().c_str());
			Term_ref r=Reduce();
			return r.ReduceApply(a1,a2,a3,a4,a5);
		}
		virtual Term_tref Globalize(Stack<EvalTerm>& stack){
			Term_ptr t;
			switch(GetState(&t)){
			case noresult:
				if(!(t=&m_t.FollowFullIndirection())->IsGlobal()){
					stack.push(this);
					stack.push(t.ptr());
					return *this;
				}else{
					t=new Global<Blackhole>(*this);
					SetState(done,t);
					return t;
				}
			case done:
				if(!(t=&t->FollowFullIndirection())->IsGlobal()){
//					stack.push(this);
					stack.push(t.ptr());
					return *this;
				}else
					return *t;
			default:
				LAMBDA_ASSERT(false,"calculating blackhole %s cannot be globalized",name().c_str());
				return *this;
			}
		}
		static void* operator new(size_t s){return Term::operator_new_t<Blackhole>(s);}
		virtual Term& FollowIndirection(){
			Term_ptr result; return GetState(&result)==done?*result:*this;}
		virtual void MarkActive(Stack<Term*>& more_active){
			if(!IsBorn()){
				LAMBDA_PRINT(gc_details,"%s not marking active",name().c_str());
			}else{
				Term_ptr result;
				bool is_done=GetState(&result)==done;
				if(!NeedMarking()){
					// already alive
					LAMBDA_ASSERT(IsGlobal()||is_done||m_t.IsAlive(),"alive %s indirects to dead %s",name().c_str(),m_t.name().c_str());
					LAMBDA_ASSERT(IsGlobal()||!is_done||result->IsAlive(),"alive %s indirects to dead result %s",name().c_str(),result->name().c_str());
				}else{
					// recursive marking
					if(is_done)
						more_active.push(result);
					else
						more_active.push(&m_t);
					Term::MarkActive(more_active);
				}
			}
		}
		virtual String name(int depth=0){
			if(!IsBorn())
				return String("unborn %clackhole@%p",IsGlobal()?'B':'b',this);
			else if(depth==-1)
				return String("%clackhole%s@%p",IsGlobal()?'B':'b',IsActive()?"!":"",this);
			else if(depth>Config::max_name_depth)
				return String("%clackhole%s@%p => ...(truncated)",IsGlobal()?'B':'b',IsActive()?"!":"",this);
			else{
				Term_ptr result;
				state_t s=GetState(&result);
				return String("%clackhole%s@%p =>%c %s",IsGlobal()?'B':'b',IsActive()?"!":"",this,(char)s,
					s==done?result->name(depth+1).c_str():m_t.name(depth+1).c_str());
			}
		}
		virtual type_t GetType(){
			Term_ptr result;
			return GetState(&result)==done?result->GetType():m_t.GetType();
		}
		virtual Term* SetIndirection(Term* ind){
			return SetState(done,ind);
		}
		virtual void DotFollow(Stack<Term*>& s){
			Term_ptr res;
			switch(GetState(&res)){
			case noresult:
			case calculating:
				s.push(&m_t);
				break;
			case done:
				s.push(res.ptr());
				break;
			default:;
			}
		}
	protected:
		state_t GetState(Term_ptr* result=NULL) {
			Term* r=GetIndirectionField();
			switch((uintptr_t)r){
			case 0:return noresult;
			case 1:return calculating;
			default:
				LAMBDA_ASSERT(r!=NULL,"NULL-result of blackhole %p",this);
				if(result)*result=r;
				return done;
			}	
		}
		Term* GetResult() {
			Term_ptr result;
			return GetState(&result)==done?result.ptr():NULL;
		}
		Term* SetState(state_t state,Term* result=NULL){
			switch(state){
			case noresult:
				result=SetIndirectionField((Term*)(uintptr_t)0);break;
			case calculating:
				if(Config::atomic_indir)
					result=SetIndirectionFieldWhen((Term*)(uintptr_t)1,(Term*)(uintptr_t)0);
				else
					SetIndirectionField((Term*)(uintptr_t)1);
				break;
			case done:			
				LAMBDA_ASSERT(result!=NULL,"NULL-result of blackhole %s",name().c_str());
				LAMBDA_VALIDATE_TERM(*result);
				if(Config::enable_stats&&GetState()==done){
					LAMBDA_ASSERT(Config::atomic_indir==false,"duplicate work in atomic mode");
					LAMBDA_PRINT(par,"duplicate work for black hole %s: %s",name().c_str(),result->name().c_str());
					Stats<>::Double();
				}
				LAMBDA_PRINT(eval,"%s reduced to %s",name().c_str(),result->name().c_str());
				SetIndirectionField(result);
				break;
			}	
			Reconcile();
			return result;
		}
	protected:
		virtual Term* SetIndirectionField(Term* t){m_result.raw()=t; return t;}
		virtual Term* SetIndirectionFieldWhen(Term* t,Term* old){m_result.raw()=t; return old;}
		virtual Term* GetIndirectionField(){return const_cast<Term*>(m_result.raw());}
		Term* SetIndirectionVolatileField(Term* t){m_result=t; return t;}
		Term* SetIndirectionVolatileFieldWhen(Term* t,Term* old){return m_result.set_when(t,old);}
		Term* GetIndirectionVolatileField(){return m_result.flush();}
	private:
		volatile_t<Term*>::type m_result;
		Term& m_t;
	};

	extern Function global_reduce_finisher;

	template <> Term_tref Global<Application>::Reduce() {
	//	entry_x();
		if(!this->IsReducable())
			return *this;
		else{
			LAMBDA_PRINT(eval,"fully reducing %s",this->name().c_str());
			Term* ind=GetIndirection();
			if(ind){
//				exit_x();
				return this->FollowFullIndirection();
			/*}else if(BaseFunction().Arguments()<=0){
//				printf("%s\n",BaseFunction().name().c_str());
				static long long c=0;
				printf("bh: %lld\n",c++);
				return *SetIndirection(&(new Blackhole(Duplicate()))->Term::Globalize());
			*/}else{
//				static long long c=0;
//				printf("r:  %lld\n",c++);
				Term_ref t=this->Duplicate();
//				worker_eval_stack().push(t);
//				exit_x();
				return global_reduce_finisher(*this)(t);
/*				Term& t=MatchMem((Term&)this->Duplicate().Reduce());
				SetIndirection(&t);
				exit_x();
				return t;*/
			}
		}
	}
	
	
	////////////////////////////////////
	// Misc

	typedef Term& T;
	
	extern Function id;

	template <typename T=Term> class Let : public Static<Constant<T> > {
	public:
		typedef Static<Constant<T> > base;
		template <typename A> Let(const A& a) : base(a) {}
	protected:
		Term& operator=(Term& rhs); //don't!
	};
	
	template <> class Let<Term> : public Static<Application> {
	public:
		typedef Static<Application> base;
		template <typename S> Let(Static<S>& a) : base(id,a) {}
	protected:
		Term& operator=(Term& rhs); //don't!
	};

#ifdef HAVE_GMP
	extern Function mpzInitI;
	extern Function mpzInitS;
	union mpzInit_t {
		mpzInit_t(lcint_t i) : i(i){}
		mpzInit_t(const char* str) : str(str){}
		lcint_t i;
		const char* str;
	};

	template <> class Let<lcmpz_t> : public Static<Application> {
	public:
		typedef Static<Application> base;
		Let(int i) :			base(mpzInitI,m_c), m_c(mpzInit_t((lcint_t)i)),	m_this(*this) {}
		Let(long i) :			base(mpzInitI,m_c), m_c(mpzInit_t((lcint_t)i)),	m_this(*this) {}
		Let(long long i) :		base(mpzInitI,m_c), m_c(mpzInit_t((lcint_t)i)),	m_this(*this) {}
		Let(const char* str) :	base(mpzInitS,m_c), m_c(mpzInit_t(str)),		m_this(*this) {}
	protected:
		Term& operator=(Term& rhs); //don't!
	private:
		Static<Constant<mpzInit_t> > m_c;
		Term_ref m_this; // make sure *base::m_indirect does not get gc'ed
	};
#endif


	static Static<Constant<> > dummy(42);
};

#endif // __LAMBDA_TERM_H
