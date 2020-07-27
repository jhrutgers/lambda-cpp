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

#ifndef __LAMBDA_PLATFORM_H
#define __LAMBDA_PLATFORM_H

#if (defined(__x86_64) || defined(__i686)) && defined(__linux)
#  define LAMBDA_PLATFORM_x86
#elif (defined(__x86_64) || defined(__i686)) && (defined(__APPLE__))
#  define LAMBDA_PLATFORM_MAC
#elif defined(__MICROBLAZE__) && defined(__HELIX__)
#  define LAMBDA_PLATFORM_MB
#else
#  error Unknown platform
#endif

#include <stddef.h>
#include <stdlib.h>

namespace lambda {
	
	struct noflush_t {};
	static noflush_t noflush;

	template <typename T> struct type_isclass {
		template <typename C> static char f(char C::*);
		template <typename C> static float f(...);
		enum { value = sizeof(f<T>(NULL))==1 };
	};
	template <typename T,bool enabled=type_isclass<T>::value> struct type_deref { typedef typeof(reinterpret_cast<T*>(NULL)->operator *())& type; };
	template <typename T> struct type_deref<T*,false> { typedef T& type; };
	template <typename T> struct type_deref<T,false> { typedef void type; };

	template <typename T,size_t alignment=sizeof(void*)>
	class mc_rw_only_t {
	public:
		mc_rw_only_t() : m_var() { flush();}
		mc_rw_only_t(noflush_t&) : m_var() {}
		mc_rw_only_t(T const & var) : m_var(var){ flush();}
		mc_rw_only_t(T const & var,noflush_t&) : m_var(var){}
		mc_rw_only_t(mc_rw_only_t const & v) : m_var((T)v){ flush();}
		mc_rw_only_t& operator=(T const & rhs){return set(rhs);}
		template <typename A> mc_rw_only_t& set(A const & rhs){flush();*const_cast<T*>(&m_var)=rhs;flush();return *this;}
		template <typename A> T set_when(A const & rhs,T const & old){flush();T res=cas(const_cast<T*>(&m_var),(T)old,(T)rhs);flush();return res;}
		mc_rw_only_t& operator=(mc_rw_only_t const & rhs){return operator=((T)rhs);}
		operator T(){return *const_cast<T*>(&m_var);}
		operator T const () const {return *const_cast<T const*>(&m_var);}
		T const * operator&() const { return const_cast<T const*>(&m_var);}
		T operator ->() const { return const_cast<T>(m_var);}
		typename type_deref<T>::type operator *() const { return *const_cast<T>(m_var); }
		mc_rw_only_t& flush();
		T get(){return operator T();}
		T volatile & raw(){return m_var;}
		T const & raw() const {return *const_cast<T const*>(&m_var);}
		static T cas(T* ptr,T const &oldval,T const &newval);
	protected:
		template <typename R> __attribute__((error("not allowed"))) operator R&(){return m_var;}
		template <typename R> __attribute__((error("not allowed"))) operator R const &() const {return m_var;}
	private:
		T volatile m_var __attribute__((aligned((const size_t)alignment)));
	} __attribute__((aligned((const size_t)alignment)));

	template <typename T> struct cv_type {
		typedef T type;					typedef T type_nc;			typedef T type_nv;			typedef T type_ncv; };
	template <typename T> struct cv_type<T const> {
		typedef T const type;			typedef T type_nc;			typedef T const type_nv;	typedef T type_ncv; };
	template <typename T> struct cv_type<T volatile> {
		typedef T volatile type;		typedef T volatile type_nc; typedef T type_nv;			typedef T type_ncv; };
	template <typename T> struct cv_type<T volatile const> {
		typedef T volatile const type;	typedef T volatile type_nc; typedef T const type_nv;	typedef T type_ncv; };


#if defined(LAMBDA_PLATFORM_x86) || defined(LAMBDA_PLATFORM_MAC)
////////////////////////////////////
// Standard PC with Linux / Mac

#ifndef LAMBDA_WORKERS
#  define LAMBDA_WORKERS 8
#endif

	template <typename T,size_t alignment> mc_rw_only_t<T,alignment>& mc_rw_only_t<T,alignment>::flush(){return *this;}

#  define HEAP_ELEM_ALIGNMENT	(sizeof(void*))
	template <typename T> struct volatile_t	{ typedef mc_rw_only_t<T> type; };
	template <typename T> struct shared_t	{ typedef mc_rw_only_t<T> type; };
#  define ATTR_SHARED_ALIGNMENT
#  define DECL_THREAD_LOCAL_PTR(type,name) static __thread type DECL_THREAD_LOCAL_PTR_NAME(name,0);
#  define DECL_THREAD_LOCAL_PTR_NAME(name,unique_id) name##__tlocal
#  define fence()						do{__sync_synchronize();asm volatile ("" : : : "memory");}while(0)
#  define atomic_cas(ptr,oldval,newval)	__sync_val_compare_and_swap(ptr,oldval,newval)
#  define atomic_add(ptr,val)			__sync_add_and_fetch(ptr,val)

#  define global_malloc(size)	malloc(size)
#  define global_free(ptr)		free(ptr)
#  define local_malloc(size)	malloc(size)
#  define local_free(ptr)		free(ptr)

#  define globalize_mem(ptr,size)		ptr
#  define globalize_dupmem(ptr,size)	({size_t s_=(size);cv_type<typeof(*ptr)>::type_nc* p_=(typeof(p_))global_malloc(s_);if(p_)memcpy(p_,ptr,s_);p_;})
#  define globalize_flushmem(ptr,size)	fence()
#  define globalize_flushall()			fence()

#  define platform_init()
#  define platform_start()
#  define platform_end()
#  define platform_exit()

#  define HAVE_GMP
//#  define HAVE_VALGRIND // set during compilation if valgrind/valgrind.h exists

#elif defined(LAMBDA_PLATFORM_MB)
////////////////////////////////////
// Starburst
#  undef LAMBDA_BACKTRACE
};

#include <sys/helix_config.h>
#include <daemons.h>
#include <profiling.h>
#include <mc.h>

namespace lambda {
#ifndef LAMBDA_WORKERS
#  define LAMBDA_WORKERS NUM_PROCESSORS
#endif

	template <typename T,size_t alignment> mc_rw_only_t<T,alignment>& mc_rw_only_t<T,alignment>::flush(){mc_flush(this->m_var);return *this;}

#  define HEAP_ELEM_ALIGNMENT	CACHELINE_ALIGN
	template <typename T> struct volatile_t	{ typedef mc_rw_only_t<T> type; };
	template <typename T> struct shared_t	{ typedef mc_rw_only_t<T,CACHELINE_ALIGN> type ATTR_DATA_ALIGNED; };
#  define ATTR_SHARED_ALIGNMENT ATTR_DATA_ALIGNED
#  define DECL_THREAD_LOCAL_PTR(type,name)				static type name##__tlocal[0] __attribute__((unused));
#  define DECL_THREAD_LOCAL_PTR_NAME(name,unique_id)	(*(typeof(name##__tlocal[0])*)((unique_id)<THREAD_LOCAL_FIELDS?&thread_local[unique_id]:NULL))

#  define fence() barrier()

#  define global_malloc(size)	smalloc(size)
#  define global_free(ptr)		sfree(ptr)
#  define local_malloc(size)	malloc(size)
#  define local_free(ptr)		free(ptr)

#  define globalize_mem(ptr,size)		({cv_type<typeof(ptr)>::type_nc p_=globalize_dupmem(ptr,size);local_free(ptr);p_;})
#  define globalize_dupmem(ptr,size)	({size_t s_=(size);cv_type<typeof(*ptr)>::type_nc* p_=(typeof(p_))global_malloc(s_);if(p_)memcpy(p_,ptr,s_);mc_flush_(p_,s_);p_;})
#  define globalize_flushmem(ptr,size)	do{mc_flush_(const_cast<typename cv_type<typeof(*(ptr))>::type_ncv*>(ptr),(size));fence();}while(0)
#  define globalize_flushall()			do{FlushDCache();fence();}while(0)

	struct cas_rpc_t { int* p; int oldval; int newval; };
	static int cas_rpc(cas_rpc_t const* a){
		mc_flush(*a);
		int res;
		int* const p=a->p;
		if((res=*p)==a->oldval)
			*p=a->newval;
		mc_flush(*p);
		mc_flush(*a);
		return res;
	}

	template <typename T>
	static T atomic_cas(T* ptr,T oldval,T newval){
		mc_flush(*ptr);
		if(sizeof(T)==sizeof(int)){
			cas_rpc_t a={reinterpret_cast<int*>(ptr),(int)oldval,(int)newval};
			mc_flush(a);
			return (T)dRemoteCall((void*(*)(void*))cas_rpc,&a,0);
		}else{
			auto_ret<bool,true>(lck_lock(ptr)<=0);
			T val=*ptr;
			if(val==oldval)
				*ptr=newval;
			mc_flush(*ptr);
			lck_unlock(ptr);
			return val;
		}
	}

	template <typename T>
	static T atomic_add(T* ptr,T val){
		mc_flush(*ptr);
		auto_ret<bool,true>(lck_lock(ptr)<=0);
		T newval=*ptr+val;
		*ptr=newval;
		mc_flush(*ptr);
		lck_unlock(ptr);
		return newval;
	}

extern "C" { int __register_exitproc(int,void*(void),void*,void*) __attribute__((weak)); }
	static int platform_boot() __attribute__((optimize("1"))); // weak symbols and -O>1 are problematic, apparently
	static int platform_boot(){
		// detect malloc bug (fix) in newlib
		if(__register_exitproc){
			uintptr_t* insns=(uintptr_t*)__register_exitproc;
			for(int i=0;i<128;i++)
				if(insns[i]==0xb60f0008) // rtsd
					break;
				else if(insns[i]==(uintptr_t)(0xb0000000|(((uintptr_t)&malloc>>16)&0xffff)) && // imm
						(insns[i+1]&0xffff)==((uintptr_t)&malloc&0xffff)){
					// malloc detected
					printf("newlib bug: __register_exitproc fails to initialize pointers properly; unsafe to continue, apply patch first\n");
					return EINVAL;
				}
		}
		return 0;
	}
	CALL_ON_BOOT(platform_boot)

	static void print_lock();
	static void print_unlock();

#  define platform_init()
	static void platform_start(){
		prf_snapshot();
	}
	static void platform_end(){
		print_lock();
		prf_dump();
		print_unlock();
	}
#  define platform_exit()

#  define HAVE_GMP

#endif
	
	template <typename T,size_t alignment> T mc_rw_only_t<T,alignment>::cas(T* ptr,T const &oldval,T const &newval){return atomic_cas(ptr,(T)oldval,(T)newval);}
};

#if defined(HAVE_VALGRIND) && defined(LAMBDA_MEMCHECK)
#  include <valgrind/valgrind.h>
#  include <valgrind/memcheck.h>
#else
#  define RUNNING_IN_VALGRIND 0
#  define VALGRIND_MAKE_MEM_NOACCESS(addr,len)
#  define VALGRIND_MAKE_MEM_UNDEFINED(addr,len)
#endif

#include <lambda/barrier.h>

#endif //  __LAMBDA_PLATFORM_H
