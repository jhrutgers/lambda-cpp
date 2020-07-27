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

#ifndef __LAMBDA_GC_MARKSWEEP_H
#define __LAMBDA_GC_MARKSWEEP_H

////////////////////////////////////
// Mark-sweep garbage collector

#include <new>

#include <lambda/config.h>
#include <lambda/debug.h>
#include <lambda/ptr.h>
#include <lambda/term.h>
#include <lambda/gc.h>

namespace lambda {

	////////////////////////////////////
	// Heap implementation

#define HEAP_ROUND_DOWN(p)		((p)&~(HEAP_ELEM_ALIGNMENT-1))
#define HEAP_ROUND_UP(p)		HEAP_ROUND_DOWN((p)+(HEAP_ELEM_ALIGNMENT-1))
#define HEAP_SIZE_ROUND_UP(s)	((size_t)(HEAP_ROUND_UP((size_t)(s))))
#define HEAP_SIZE_ROUND_DOWN(s)	((size_t)(HEAP_ROUND_DOWN((size_t)(s))))
#define HEAP_PTR_ALIGN(s)		((void*)(HEAP_ROUND_UP((uintptr_t)(s))))

	class HeapElement {
	public:
		HeapElement(size_t size) : m_size(size-sizeof(HeapElement)), m_next(NULL) {}
		void SetNext(HeapElement* next){m_next=next;}
		inline size_t Size() const {return m_size;}
		inline size_t Sizeof() const {return sizeof(HeapElement)+Size();}
		HeapElement* Next(){return m_next;}
		Term* GetTerm(){return m_term;}
		void SetTerm(Term* term){m_term=term;}
		HeapElement* Split(size_t s){
			s=HEAP_SIZE_ROUND_UP(s);
			LAMBDA_ASSERT(s<=m_size,"Invalid size %lu, expected at least %lu",s,m_size);
			if(HEAP_SIZE_ROUND_DOWN(m_size-s)>HEAP_SIZE_ROUND_UP(sizeof(HeapElement)+sizeof(Term)+sizeof(void*)*4)){
				// split this element
				size_t new_size=HEAP_SIZE_ROUND_DOWN(m_size-s-sizeof(HeapElement));
				HeapElement* he_new=new(&m_buf[new_size]) HeapElement(m_size-new_size);
//				printf("Splitting %p(%lu) -> %p(%lu) + %p(%lu)\n",this,m_size,this,new_size,he_new,he_new->Size());
				m_size=new_size;
				return he_new;
			}else
				return NULL;
		}
		bool Merge(HeapElement* e){
			if((uintptr_t)e==(uintptr_t)&m_buf[m_size]){
				m_size+=e->Sizeof();
				return true;
			}else
				return false;
		}
		void* Buf(){return m_buf;}
		HeapElement* thrash(){
#ifdef LAMBDA_DEBUG
			LAMBDA_ASSERT(this!=NULL,"invalid heap element to thrash");
			memset(m_buf,0xe0,Size());
			if(Size()>=sizeof(int)*2)
				reinterpret_cast<int*>(m_buf)[1]=worker_id(); // for LAMBDA_VALIDATE_TERM
			m_term=NULL;
			fence();
#endif
			VALGRIND_MAKE_MEM_UNDEFINED(m_buf,Size());
			return this;
		}
	private:
		size_t m_size;
		HeapElement* m_next;
		Term* m_term;
		char m_buf[] __attribute__((aligned(HEAP_ELEM_ALIGNMENT)));
	} __attribute__((aligned(HEAP_ELEM_ALIGNMENT)));

	class HeapElementList {
	public:
		class Iterator {
		public:
			Iterator(HeapElement* &head) : m_head(head), m_prev(NULL), m_cur(head) {}
			Iterator& operator=(const Iterator& rhs){
				this->~Iterator();
				new(this) Iterator(rhs);
				return *this;
			}
			inline HeapElement* cur(){return m_cur;}
			inline HeapElement* operator*(){return cur();}
			inline operator HeapElement*(){return cur();}
			HeapElement* next(){
				if(m_cur){
					m_prev=m_cur;
					m_cur=m_cur->Next();
				}
				return m_cur;
			}
			HeapElement* drop(){
				HeapElement* res=m_cur;
				if(m_cur){
					m_cur=res->Next();
					if(m_prev){
						m_prev->SetNext(m_cur);
					}else{
						LAMBDA_ASSERT(m_head==res,"invalid drop: %p vs (%p<) %p (<%p)",m_head,m_prev,res,res->Next());
						m_head=m_cur;
					}
				}else
					LAMBDA_ASSERT(m_head==NULL,"invalid drop of empty list");
				return res;
			}
			HeapElement* use(size_t s){
				HeapElement* e=cur();
				LAMBDA_ASSERT(e->Size()>=s,"invalid alloc");
				HeapElement* e_use=e->Split(s);
				if(likely(e_use!=NULL))
					return e_use;
				else
					return drop();
			}
			bool canMove(HeapElement* e){
				// return true when e is cur() or (possibly) later in list
				return
					!m_head || // empty list is ok
					(m_prev&&!m_cur&&(uintptr_t)m_prev<=(uintptr_t)e) || // at end of list, ok when prev <= e
					(!m_prev&&m_cur) || // at begin of list, always ok
					(m_cur&&(uintptr_t)m_cur<=(uintptr_t)e); // in middle of list, ok when cur <= e
			}
			Iterator& move(HeapElement* e){
				HeapElement *c,*n;
				while((c=cur())&&(n=c->Next())&&(uintptr_t)n<=(uintptr_t)e)
					next();
				LAMBDA_ASSERT(
					cur()==m_head||
					((cur()==NULL||(uintptr_t)cur()<=(uintptr_t)e)&&
					(cur()==NULL||cur()->Next()==NULL||(uintptr_t)e<(uintptr_t)cur()->Next())),
					"invalid find: %p <= %p < %p",cur(),e,cur()?cur()->Next():NULL);
				return *this;
			}
			// makes e==cur() or e==cur()->Next()
			HeapElement* insert(HeapElement* e){
				move(e);
				LAMBDA_ASSERT(cur()==NULL||(e!=cur()&&e!=cur()->Next()),"already inserted");
				if(likely(m_prev&&m_cur)){
					//somewhere in the middle
					e->SetNext(m_cur->Next());
					m_cur->SetNext(e);
				}else if(!m_prev&&m_cur){
					LAMBDA_ASSERT(m_cur==m_head,"invalid head: %p vs %p",m_cur,m_head);
					if((uintptr_t)m_cur<(uintptr_t)e){
						// insert after cur (which is the head)
						e->SetNext(m_cur->Next());
						m_cur->SetNext(e);
					}else{
						// insert before cur (as head)
						e->SetNext(m_cur);
						m_head=m_cur=e;
					}
				}else if(m_prev&&!m_cur){
					// insert at end of list
					m_prev->SetNext(e);
					e->SetNext(NULL);
					m_cur=e;
				}else if(!m_prev&&!m_cur){
					// empty list, create one
					LAMBDA_ASSERT(m_head==NULL,"invalid empty list");
					e->SetNext(NULL);
					m_head=m_cur=e;
				}else
					LAMBDA_ASSERT(false,"invalid state");
				return e;
			}
			void merge(HeapElement* e){
				move(e);
				LAMBDA_ASSERT(cur()==NULL||(e!=cur()&&e!=cur()->Next()),"already inserted");
				if(!cur()||!cur()->Merge(e)){
					insert(e);
					move(e);
					LAMBDA_ASSERT(cur()==e,"invalid insert: %p vs %p",cur(),e);
				}
				HeapElement* n=cur()->Next();
				if(n){
					HeapElement* nn=n->Next();
					if(cur()->Merge(n))
						cur()->SetNext(nn);
				}
			}
			void check(){
#ifdef LAMBDA_DEBUG
				LAMBDA_ASSERT(m_head==m_cur&&m_prev==NULL,"invalid head: %p vs %p",m_head,m_cur);
				HeapElement* p=NULL;
				while(m_cur){
					LAMBDA_ASSERT(m_prev==p,"invalid next: %p vs %p",m_prev,p);
					LAMBDA_ASSERT((uintptr_t)m_prev<(uintptr_t)m_cur,"unsorted list: %p < %p",m_prev,m_cur);
					p=m_cur;
					next();
				}
#endif
			}
			Iterator& operator ++(int){next();return *this;}
			Iterator& operator ++(){next();return *this;}
		private:
			HeapElement* &m_head;
			HeapElement* m_prev;
			HeapElement* m_cur;
		};

		HeapElementList() : m_head(NULL),m_it(m_head) {}
		Iterator& getIterator(){
			return m_it;
		}
		Iterator& iterate(){
			return (m_it=Iterator(m_head));
		}
		Iterator& insert(HeapElement* e){
			if(!m_it.canMove(e))
				iterate();
			m_it.insert(e);
			return m_it;
		}
		Iterator& merge(HeapElement* e){
			if(!m_it.canMove(e))
				iterate();
			m_it.merge(e);
			return m_it;
		}
		HeapElement* remove(HeapElement* e){
			if(!m_it.canMove(e))
				iterate();
			m_it.move(e);
			LAMBDA_ASSERT(m_it.cur()==e,"non-existent remove");
			m_it.drop();
			return e;
		}
		bool isEmpty(){return m_head==NULL;}
		int size(){
			int res=0;
			for(Iterator& it=iterate();*it;it++)res++;
			return res;
		}
		void print(const char* label=NULL){
			if(!label)label="List";
			printf("%s:\n",label);
			for(Iterator it=iterate();it.cur();it++){
				HeapElement* e=it.cur();
				printf("%s: %p(%lu) = %p\n",label,e,e->Size(),e->GetTerm());
			}
			printf("(end list)\n");
		}
		void check(){
#ifdef LAMBDA_DEBUG
			Iterator(m_head).check();
#endif
		}
	private:
		HeapElement* m_head;
		Iterator m_it;
	};

	class MacroBlock {
	public:
		MacroBlock(MacroBlock* next=NULL) : m_next(next) {}
		HeapElement* Init(){return new(buf) HeapElement(sizeof(buf)); }
		static MacroBlock* Alloc(size_t s){
			void* m=global_malloc(s+HEAP_ELEM_ALIGNMENT+sizeof(void*));
			if(!m)return NULL;
			void* p=HEAP_PTR_ALIGN((uintptr_t)m+sizeof(void*));
			((void**)p)[-1]=m;
			return (MacroBlock*)p;
		}
		static void* operator new(size_t s){
			void* m=Alloc(s);
			if(!m)
				Error("cannot malloc macroblock");
			return m;
		}
		static void* operator new(size_t s,void* buf){
			return buf;
		}
		static void operator delete(void* p){
			global_free(((void**)p)[-1]);
		}
		MacroBlock* GetNext(){return m_next;}
	private:
		MacroBlock* m_next;
		char buf[sizeof(HeapElement)+Config::macroblock_size] __attribute__((aligned(HEAP_ELEM_ALIGNMENT)));
	} __attribute__((aligned(HEAP_ELEM_ALIGNMENT)));
	
	////////////////////////////////////
	// Mark-sweep GC heap

	template <>
	class Heap<Config::gc_mark_sweep> {
	public:
		Heap() : m_mbs(NULL), m_free(), m_local(), m_global(), m_other(), m_marking() {}
		~Heap(){
			MacroBlock* m=m_mbs,*n;
			while(m){
				n=m->GetNext();
				delete m;
				m=n;
			}
		}
		template <typename T> void* Alloc(size_t s){
			LAMBDA_ASSERT(sizeof(T)==s||(is_type<T,NoTerm>::value),"allocing %lu bytes for a type of %lu bytes",s,sizeof(T));
			LAMBDA_ASSERT(s<sizeof(MacroBlock)/2,"MacroBlock too small to allocate %lu bytes",s);
#ifdef LAMBDA_DEBUG
			worker_check_stack();
#endif

			HeapElementList::Iterator& it=m_free.getIterator();
			while(true){
				if(unlikely(!it.cur())){
					LAMBDA_PRINT(mem,"cannot allocate %lu bytes for %s, try GC",s,typeid(T).name());
					if(worker_inspect_state()||DoGC(true)){
						// got much memory back
						m_free.iterate();
					}else{
						LAMBDA_PRINT(mem,"really need more memory");
						AllocMB();
					}
				}else if(likely(it.cur()->Size()>=s)){
					// right size, use this one
					HeapElement* e=it.use(s);
					void* res=e->Buf();
					if(likely((!is_type<T,NoTerm>::value))){
						e->SetTerm(static_cast<Term*>(reinterpret_cast<T*>(res)));
						m_new.insert(e);
						LAMBDA_PRINT(mem,"allocated mem for %s@%p, &Term=%p",typeid(T).name(),res,e->GetTerm());
					}else{
						e->SetTerm((Term*)(uintptr_t)1);
						m_other.insert(e);
						LAMBDA_PRINT(mem,"allocated mem for other %p, size %lu",res,s);
					}
					VALGRIND_MAKE_MEM_UNDEFINED(res,s);
					return res;
				}else
					it++;
			}
		}

		void Free(void* p){
			if(p){
				HeapElement* e=(HeapElement*)((uintptr_t)p-sizeof(HeapElement));
				LAMBDA_ASSERT((uintptr_t)e->GetTerm()==(uintptr_t)1,"double or invalid free of %p",p);
				e->SetTerm(NULL);
				LAMBDA_PRINT(mem,"not really freeing %p, wait for GC",p);
				VALGRIND_MAKE_MEM_NOACCESS(p,e->Size());
			}
		}
		
		bool DoGC(bool only_local){
			if(only_local&&m_new.isEmpty()&&m_local.isEmpty())
				return false;//nothing to collect

			LAMBDA_PRINT(gc,"Start %s GC...",only_local?"local":"global");
			VCDDump<>::state_t vcd_old=worker_set_vcd(only_local?VCDDump<>::local_gc:VCDDump<>::global_gc);
			size_t total_free=0;

			if(!only_local){
				globalize_flushall();
				if(gc_barrier_wait(true))
					LAMBDA_PRINT(gc_details,"marking all globals old...");
				// unmark all globals
				for(HeapElementList::Iterator &it=m_global.iterate();*it;it++)
					it.cur()->GetTerm()->MarkOld();
				// and all possibly new globals
				for(HeapElementList::Iterator &it=m_new.iterate();*it;it++)
					it.cur()->GetTerm()->MarkOld();
				globalize_flushall();
				if(gc_barrier_wait()){
					LAMBDA_PRINT(gc_details,"marking all active terms and cleaning locals...");
					queue_mark_active(m_marking);
				}
			}

			// mark reachables from roots
			for(Term_tptr* p=term_stack;p;p=p->peek()){
				Term* t=p->ptr();
				LAMBDA_PRINT(gc_details,"Root: %p -> %p",p,t);
				if(t)
					m_marking.push(t);
			}
			// mark all terms that are under evaluation
			worker_eval_stack().dup(m_marking);
			// mark all terms that are followed by dot
			dot_marked(m_marking);
			// mark alive
			LAMBDA_PRINT(gc_details,"marking...");
			Term *t;
			while((t=m_marking.pop())){
				LAMBDA_VALIDATE_TERM(*t);
				t->MarkActive(m_marking);
			}

			// clean dead locals
			for(HeapElementList::Iterator &it_local=m_local.iterate(),&it_free=m_free.iterate();*it_local;){
				HeapElement* e=it_local.cur();
				Term* t=e->GetTerm();
				LAMBDA_ASSERT(t!=NULL,"element %p does not point to a term",e);
				LAMBDA_ASSERT(!t->IsGlobal(),"global %s on local list",t->name().c_str());
				if(t->IsAlive()){
					LAMBDA_PRINT(gc_details,"local %p is still alive",t);
					t->MarkOld();
					it_local++;
				}else{
					LAMBDA_PRINT(gc_details,"local %p is dead",t);
					t->~Term();
					total_free+=e->Size();
					it_free.merge(it_local.drop()->thrash());
				}
			}
		
			// check new elements
			for(HeapElementList::Iterator &it_new=m_new.iterate(),&it_local=m_local.iterate(),&it_global=m_global.iterate(),&it_free=m_free.iterate();*it_new;){
				HeapElement* e=it_new.cur();
				Term* t=e->GetTerm();
				LAMBDA_ASSERT(t!=NULL,"element %p does not point to a term",e);
				if(!t->IsBorn()){
					LAMBDA_PRINT(gc_details,"new %p is unborn, skipping",t);
					it_new++;
				}else if(t->IsGlobal()){
					LAMBDA_PRINT(gc_details,"new %p is global and assumed to be alive",t);
					it_global.insert(it_new.drop());
				}else if(t->IsAlive()){
					LAMBDA_PRINT(gc_details,"new %p is local and alive",t);
					it_local.insert(it_new.drop());
					t->MarkOld();
				}else{
					LAMBDA_PRINT(gc_details,"new %p is local and dead",t);
					t->~Term();
					total_free+=e->Size();
					it_free.merge(it_new.drop()->thrash());
				}
			}

			if(!only_local){
				globalize_flushall();
				if(gc_barrier_wait())
					LAMBDA_PRINT(gc_details,"cleaning globals...");
				// clean globals
				for(HeapElementList::Iterator &it_global=m_global.iterate(),&it_free=m_free.iterate();*it_global;){
					HeapElement* e=it_global.cur();
					Term* t=e->GetTerm();
					LAMBDA_ASSERT(t!=NULL,"element %p does not point to a term",e);
					LAMBDA_ASSERT(t->IsGlobal(),"local %s on global list",t->name().c_str());
					if(t->IsAlive()){
						LAMBDA_PRINT(gc_details,"global %p is still alive",t);
						t->MarkOld();
						it_global++;
					}else{
						LAMBDA_PRINT(gc_details,"global %p is dead",t);
						t->~Term();
						total_free+=e->Size();
						it_free.merge(it_global.drop()->thrash());
					}
				}
				globalize_flushall();
				if(gc_barrier_wait())
					LAMBDA_PRINT(gc_details,"global GC done");
			}

			// clean NoTerms
			for(HeapElementList::Iterator &it_other=m_other.iterate(),&it_free=m_free.iterate();*it_other;){
				HeapElement* e=it_other.cur();
				if((uintptr_t)e->GetTerm()==(uintptr_t)0){
					LAMBDA_PRINT(gc_details,"other %p is dead",e->Buf());
					VALGRIND_MAKE_MEM_UNDEFINED(e->Buf(),e->Size());
					total_free+=e->Size();
					it_free.merge(it_other.drop()->thrash());
				}else
					it_other++;
			}

			// done
			LAMBDA_PRINT(gc,"GC done, free=%d, new=%d, local=%d, global=%d, other=%d",m_free.size(),m_new.size(),m_local.size(),m_global.size(),m_other.size());
			LAMBDA_PRINT(mem,"GC freed > %lu bytes",total_free);
			worker_set_vcd(vcd_old);
			return total_free>Config::macroblock_size/2;
		}
	protected:
		HeapElementList::Iterator& AllocMB(){
			void* m=MacroBlock::Alloc(sizeof(MacroBlock));
			if(!m){
				m_marking.cleanup(); //try to free some more memory
				worker_eval_stack().cleanup();
				LAMBDA_PRINT(mem,"cannot allocate macroblock, try global GC");
				if(!gc_trigger_global())
					Error("out of memory");
				LAMBDA_PRINT(mem,"freed enough memory to continue without additional macroblock");
				return m_free.iterate();
			}else{
				m_mbs=new(m) MacroBlock(m_mbs);
				Stats<>::Macroblock();
				LAMBDA_PRINT(mem,"new MacroBlock %p of size 0x%lx",m_mbs,Config::macroblock_size);
				return m_free.insert(m_mbs->Init());
			}
		}
	private:
		MacroBlock* m_mbs;
		HeapElementList m_free;
		HeapElementList m_new;
		HeapElementList m_local;
		HeapElementList m_global;
		HeapElementList m_other;
		Stack<Term*> m_marking;
	};
};

#endif
