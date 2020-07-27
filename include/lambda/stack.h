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

#ifndef __LAMBDA_STACK_H
#define __LAMBDA_STACK_H

////////////////////////////////////
////////////////////////////////////
// Term stack
////////////////////////////////////
////////////////////////////////////

#include <lambda/config.h>
#include <lambda/debug.h>

namespace lambda {
	template <typename T>
	class Stack {
	public:
		class Chunk {
		public:
			Chunk(Chunk* prev=NULL) : m_prev(prev), m_next(NULL), m_ptr(0) {
				if(m_prev)
					m_prev->m_next=this;
			}
			~Chunk(){
				drop();
			}
			static void* operator new(size_t s){
				void* m=local_malloc(s);
				if(!m)
					Error("out of memory for stack chunk");
				return m;
			}
			static void operator delete(void* p){
				local_free(p);
			}
			Chunk* pop(){
				LAMBDA_ASSERT(m_ptr<=Config::stack_chunk_size,"addressing wrong stack chunk for pop");
				if(unlikely(m_ptr==0)){
					LAMBDA_ASSERT(m_prev!=NULL,"popping from empty stack");
					return m_prev->pop();
				}else{
					m_ptr--;
					return this;
				}
			}
			T swap(T const & t){
				LAMBDA_ASSERT((m_ptr>0||m_prev!=NULL)&&m_ptr<=Config::stack_chunk_size,"addressing wrong stack chunk for swap");
				if(m_ptr==0)
					return m_prev->swap(t);
				else{
					T res=m_stack[m_ptr-1];
					m_stack[m_ptr-1]=t;
					return res;
				}
			}
			T* peek(){
				LAMBDA_ASSERT(m_ptr<=Config::stack_chunk_size,"addressing wrong stack chunk for peek");
				if(unlikely(m_ptr==0))
					return m_prev?m_prev->peek():NULL;
				else
					return &m_stack[m_ptr-1];
			}
			T* top(){
				if(m_ptr==0){
					if(m_prev==NULL)
						return NULL;
					else
						return m_prev->top();
				}else
					return &m_stack[m_ptr-1];
			}
			T* bottom(){
				return m_ptr==0?NULL:&m_stack[0];
			}
			Chunk* push(T const & t){
				if(unlikely(m_ptr==Config::stack_chunk_size)){
					if(m_next)
						return m_next->push(t);
					else{
						LAMBDA_PRINT(mem,"adding successor to stack chunk %p",this);
						return (new Chunk(this))->push(t);
					}
				}else{
					m_stack[m_ptr++]=t;
					return this;
				}
			}
			Chunk* drop(){
				if(m_next)
					m_next->m_prev=m_prev;
				if(m_prev)
					m_prev->m_next=m_next;
				Chunk* res=m_prev?m_prev:m_next;
				m_prev=NULL;
				m_next=NULL;
				return res;
			}
			bool full(){
				return m_ptr==Config::stack_chunk_size;
			}
			bool empty(){
				return m_ptr==0;
			}
			template <typename A>
			Chunk* dup(Stack<A>& s){
				for(unsigned int p=0;p<m_ptr;p++)
					s.push((A)m_stack[p]);
				return next();
			}
			Chunk* prev(){ return m_prev; }
			Chunk* next(){ return m_next; }
			bool withinRange(T* t){
				return
					(uintptr_t)t >= (uintptr_t)&m_stack[0] &&
					(uintptr_t)t <= (uintptr_t)&m_stack[Config::stack_chunk_size-1];
			}
		private:
			Chunk* m_prev;
			Chunk* m_next;
			unsigned int m_ptr;
			T m_stack[Config::stack_chunk_size];
		};

		Stack() : m_chunk(new Chunk()) {}
		~Stack() {
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			flush();
		}
		void push(T const & t){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			m_chunk=m_chunk->push(t);
		}
		T pop(){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			T* t=m_chunk->peek();
			if(t){
				T res=*t;
				m_chunk=m_chunk->pop();
				return res;
			}else
				return T();
		}
		T swap(T const & t){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			return m_chunk->swap(t);
		}
		T* top(){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			return m_chunk->top();
		}
		T* bottom(){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			Chunk* c=m_chunk;
			while(c->prev())c=c->prev();
			return c->bottom();
		}
		// return 0 when old_top==top(), -1 when old_top has been pop()'ed already (stack has shrunk), 1 when still on stack (stack has grown)
		int compare(T* old_top){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			T* current_top=top();
			if(current_top==old_top){
				// did not change
				return 0;
			}else if(m_chunk->withinRange(old_top)){
				// within same chunk
				if((uintptr_t)current_top<(uintptr_t)old_top)
					return -1;
				else
					return 1;
			}else if(old_top==NULL && current_top!=NULL){
				return 1;
			}else if(old_top!=NULL && current_top==NULL){
				return -1;
			}else{
				// try to find in prev chunks
				for(Chunk* c=m_chunk->prev();c;c=c->prev())
					if(c->withinRange(old_top))
						// in prev chunk
						return 1;
				// in next chunk (although that chunk might haven been cleanup()'ed yet)
				return -1;
			}
		}
		T* peek(){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			return m_chunk->peek();
		}
		void cleanup(){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			Chunk* c=m_chunk;
			// make sure that we do not clean the last remaining stack position (Term::FullReduce() keeps track of the old stack top)
			if(m_chunk->next()&&m_chunk->full()){
				LAMBDA_ASSERT(m_chunk->next()->empty(),"cleaning wrong chunk of stack %p",this);
				c=m_chunk->next();
			}
			while(c->next()){
				LAMBDA_PRINT(mem,"dropping stack %p chunk %p",this,m_chunk->next());
				delete c->next();
			}
		}
		void flush(){
			Chunk *c=m_chunk,*n;
			while(c){
				n=c->drop();
				delete c;
				c=n;
			}
		}
		template <typename A>
		void dup(Stack<A>& s){
			LAMBDA_ASSERT(m_chunk!=NULL,"chunkless stack %p",this);
			Chunk* c=m_chunk;
			// go to first
			while(c->prev())c=c->prev();
			// append all elements
			while(c)c=c->dup(s);
		}
	private:
		Chunk* m_chunk;
	};
};

#endif // __LAMBDA_STACK_H
