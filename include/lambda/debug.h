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

#ifndef __LAMBDA_DEBUG_H
#define __LAMBDA_DEBUG_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#if defined(LAMBDA_PLATFORM_x86) || defined(LAMBDA_PLATFORM_MAC)
#  include <execinfo.h>
#endif
#include <string.h>

#include <lambda/config.h>

#ifndef unlikely
#  define unlikely(expr) __builtin_expect(expr,0)
#endif
#ifndef likely
#  define likely(expr) __builtin_expect(expr,1)
#endif

#ifndef STRINGIFY
#  define XSTRINGIFY(s) #s
#  define STRINGIFY(s) XSTRINGIFY(s)
#endif
#ifndef N_ARGS
#  define N_ARGS(args...)			N_ARGS_HELPER1(args, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#  define N_ARGS_0N(args...)		N_ARGS_HELPER1(args, N, N, N, N, N, N, N, N, 0)
#  define N_ARGS_01N(args...)		N_ARGS_HELPER1(args, N, N, N, N, N, N, N, 1, 0)
#  define N_ARGS_HELPER1(args...)	N_ARGS_HELPER2(args)
#  define N_ARGS_HELPER2(x1, x2, x3, x4, x5, x6, x7, x8, x9, n, x...) n
#endif
#ifndef PP_CONCAT
#  define PP_CONCAT(a,b)			PP_CONCAT_(a,b)
#  define PP_CONCAT_(a,b)			a ## b
#endif
#ifndef PP_CONCAT_N
#  define PP_CONCAT_N(a,b...)		PP_CONCAT(a,N_ARGS(b))
#  define PP_CONCAT_0N(a,b...)		PP_CONCAT(a,N_ARGS_0N(b))
#  define PP_CONCAT_01N(a,b...)		PP_CONCAT(a,N_ARGS_01N(b))
#endif
#ifndef PP_CALL
#  define PP_CALL(f,a...)			PP_CALL_(f,##a)
#  define PP_CALL_(f,a...)			f(a)
#endif

#define LAMBDA_REST_ARG_0()
#define LAMBDA_REST_ARG_N(a1,a...)	,a
#define LAMBDA_REST_ARG(a...)		PP_CALL(PP_CONCAT_0N(LAMBDA_REST_ARG_,##a),a)
#define LAMBDA_FIRST_ARG_0()
#define LAMBDA_FIRST_ARG_N(a1,a...)	a1
#define LAMBDA_FIRST_ARG(a...)		PP_CALL(PP_CONCAT_0N(LAMBDA_FIRST_ARG_,##a),a)
#define LAMBDA_MSG_0(prefix)
#define LAMBDA_MSG_N(prefix,a1,a...) prefix a1
#define LAMBDA_MSG(prefix,a...)		PP_CALL(PP_CONCAT_0N(LAMBDA_MSG_,##a),prefix,##a)

#define LAMBDA_ASSERT_R(expr,msg,a...)															\
	do{																							\
		if(unlikely(!(expr)))																	\
			lambda::Error("ASSERTION: in %s(), %s:%d: " msg,__func__,__FILE__,__LINE__,##a);	\
	}while(0)

#ifdef LAMBDA_DEBUG
#  define LAMBDA_ASSERT(expr,msg...)															\
	do{																							\
		if(lambda::Config::enable_assert)														\
			LAMBDA_ASSERT_R(expr,msg);															\
	}while(0)
#  define LAMBDA_PRINT(cat,msg,a...)															\
	do{																							\
		if(	lambda::Config::enable_debug && lambda::Config::debug_##cat!=-1)					\
			lambda::Debug(lambda::Config::debug_##cat,"%-10s: " msg,STRINGIFY(cat),##a);		\
	}while(0)
#  define LAMBDA_PRINT_IF(cat,expr,msg,a...)													\
	do {																						\
		if((expr))																				\
			LAMBDA_PRINT(cat,msg,##a);															\
	}while(0)
#  define LAMBDA_VALIDATE_TERM(t,msg...)														\
	LAMBDA_ASSERT(																				\
		&(t)!=NULL &&																			\
		*const_cast<volatile unsigned int*>(reinterpret_cast<unsigned int*>(&(t)))!=0xe0e0e0e0,	\
		"term %p (`" STRINGIFY(t) "', owned by worker %d) is invalid" LAMBDA_MSG("; ",##msg),	\
		&(t),reinterpret_cast<int*>(&(t))[1] LAMBDA_REST_ARG(msg))
#else
#  define LAMBDA_ASSERT(...)
#  define LAMBDA_PRINT(cat,msg,a...)															\
	do{																							\
		if(	lambda::Config::debug_##cat!=-1 &&													\
			lambda::Config::debug_##cat==lambda::Config::debug_always)							\
			printf(msg "\n",##a);																\
	}while(0)
#  define LAMBDA_PRINT_IF(...)
#  define LAMBDA_VALIDATE_TERM(...)
#endif

namespace lambda {

#ifdef LAMBDA_DEBUG
	static pthread_mutex_t print_mutex=PTHREAD_MUTEX_INITIALIZER;
	static void print_lock(){
		int res __attribute__((unused))=pthread_mutex_lock(&lambda::print_mutex);}
	static void print_unlock(){
		pthread_mutex_unlock(&lambda::print_mutex);}
#else
	static void print_lock(){}
	static void print_unlock(){}
#endif
	
	static int worker_id();
	static void worker_check_stack();

	class Error {
	public:
		Error(const char* msg, ...) throw() __attribute__((noreturn,format(printf,2,3))) {
			va_list args;
			va_start(args,msg);
			print_lock();
			fprintf(stdout,"\n%s!! LC error (worker %d): ",Config::enable_colors?"\x1b[37;1;41m":"",worker_id());
			vfprintf(stdout,msg,args);
			if(Config::enable_colors)
				fprintf(stdout,LAMBDA_DEFAULT_COLOR "\n");
			else
				fprintf(stdout,"\n");
			va_end(args);

			fflush(NULL);
#ifdef LAMBDA_BACKTRACE
			// stack trace
			void *sym_array[64];
			size_t depth=backtrace(sym_array, sizeof(sym_array)/sizeof(void*));
			backtrace_symbols_fd(sym_array,depth,1);
			if(depth==sizeof(sym_array)/sizeof(void*))
				fprintf(stdout,"...\n");
			fflush(NULL);
#endif
			print_unlock();
			worker_check_stack();
			abort();
		}
	};
	
	static void worker_dump_stdout(const char* s);

	class Debug {
	public:
		Debug(int color,const char* msg, ...) throw() __attribute__((format(printf,3,4))) {
#ifdef LAMBDA_DEBUG
			va_list args;
			va_start(args,msg);
			print_lock();
			if(Config::enable_colors&&color!=-1)
				fprintf(stdout,"\x1b[%d%sm",30+(color%8),color>=8?";1":"");
			fprintf(stdout,"LC (w %2d): ",worker_id());
			if(Config::enable_vcd_stdout){
				char* m;
				if(vasprintf(&m,msg,args)!=-1){
					fputs(m,stdout);
					worker_dump_stdout(m);
					free(m);
				}
			}else
				vfprintf(stdout,msg,args);
			if(Config::enable_colors)
				fprintf(stdout,LAMBDA_DEFAULT_COLOR "\n");
			else
				fprintf(stdout,"\n");
			print_unlock();
			va_end(args);
#endif
		}
	};

	class String {
	public:
		String(bool plain,const char* s){
			m_s=strdup(s);
		}
		String(const char* fmt, ...) throw() __attribute__((format(printf,2,3))) {
			va_list args;
			va_start(args,fmt);
			if(vasprintf(&m_s,fmt,args)==-1)
				m_s=NULL;
			va_end(args);
		}
		String(const String& s){
			m_s=strdup(s.c_str());
		}
		~String(){
			if(m_s)
				free(m_s);
		}
		const char* c_str() const {
			return m_s?m_s:"(?)";
		}
		operator const char*() const {
			return c_str();
		}
		void Reconcile() const {
			if(m_s)
				globalize_flushmem(m_s,strlen(m_s)+1);
		}
	private:
		char* m_s;
	};


	template <typename T1,typename T2> struct is_type { enum { value = false }; };
	template <typename T1> struct is_type<T1,T1> { enum { value = true }; };
};

#endif // __LAMBDA_DEBUG_H
