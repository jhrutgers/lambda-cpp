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

#ifndef __LAMBDA_CONFIG_H
#define __LAMBDA_CONFIG_H

#ifndef __cplusplus
#  error C++ required for lambda
#endif

#if !defined(__GNUC__) || __GNUC__ < 4 || __GNUC_MINOR__< 4 
#  error GNU g++ >= 4.4 required
#endif

#if defined(LAMBDA_WORKERS) && LAMBDA_WORKERS<1
#  error Invalid number of processors specified
#endif

#ifdef LAMBDA_BENCHMARK
#  undef LAMBDA_DEBUG
#  undef LAMBDA_MEMCHECK
#  undef LAMBDA_TEST_ATOMIC_QUEUE
#  undef LAMBDA_TEST_ATOMIC_INDIR
#endif

////////////////////////////////////
////////////////////////////////////
// Config
////////////////////////////////////
////////////////////////////////////

//#define LAMBDA_DEBUG

#include <lambda/platform.h>

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <complex.h>
#ifdef HAVE_GMP
#  include <gmp.h>
#endif

#ifndef max
#  define max(a,b)	((a)>=(b)?(a):(b))
#endif

namespace lambda {
	typedef int32_t lcint_t;
	typedef double lcfloat_t;
	typedef double complex lccomplex_t;
#ifdef HAVE_GMP
	typedef mpz_t lcmpz_t;
#else
	typedef lcint_t lcmpz_t;
#endif

	struct Config {
#if defined(LAMBDA_BENCHMARK)
		////////////////////////////////
		// all off during benchmark
		static const bool enable_stats				= false;
		static const bool enable_debug				= false;
		static const bool enable_assert				= false;
		static const bool enable_dot				= false;
		static const bool enable_vcd				= false;
		
#elif defined(LAMBDA_DEBUG)
		////////////////////////////////
		// Debug mode

		static const bool enable_stats				= true;
		static const bool enable_debug				= true;
		static const bool enable_assert				= true;
#  if defined(LAMBDA_PLATFORM_MB)
		static const bool enable_dot				= false;
		static const bool enable_vcd				= false;
#  elif defined(LAMBDA_PLATFORM_MAC)
		static const bool enable_dot				= false;
		static const bool enable_vcd				= true;
#  else
		static const bool enable_dot				= true;
		static const bool enable_vcd				= true;
#  endif
		
#elif defined(LAMBDA_PLATFORM_MB)
		////////////////////////////////
		// Release mode
		static const bool enable_stats				= false;
		static const bool enable_debug				= false;
		static const bool enable_assert				= false;
		static const bool enable_dot				= false;
		static const bool enable_vcd				= false;
#else
		static const bool enable_stats				= true;
		static const bool enable_debug				= false;
		static const bool enable_assert				= false;
		static const bool enable_dot				= false;
		static const bool enable_vcd				= true;
#endif
		
		////////////////////////////////
		// All modes
		static const bool enable_vcd_stdout			= true && enable_vcd;
		static const bool enable_colors				= true;

		// ANSI colors, bright is +8, -1 means disabled 
		static const int debug_prog					= -1;
		static const int debug_vars					= -1;
		static const int debug_mem					= -1;//3;
		static const int debug_refs					= -1;
		static const int debug_gc					= 11;
		static const int debug_gc_details			= -1;//5;
		static const int debug_code					= 8;
		static const int debug_eval					= -1;//6;
		static const int debug_par					= 13;
		static const int debug_queue				= 12;
		static const int debug_worker				= 10;
		static const int debug_globals				= -1;
		static const int debug_state				= -1;
		static const int debug_user					= 8;
		static const int debug_misc					= -1;
		static const int debug_always				= 7; // should be unique from all other debug_* (or -1)
		
		static const bool dot_all					= false;
		static const bool dot_timed					= false;
		static const int dot_timed_interval_ms		= 1000;

		static const unsigned int workers			= enable_dot&&(dot_all||dot_timed)?1:LAMBDA_WORKERS;
		static const useconds_t worker_idle_sleep_min = 0x800;
		static const useconds_t worker_idle_sleep_max = 0x10000;
		static const unsigned int term_queue_size	= 10240;
#ifdef LAMBDA_TEST_ATOMIC_QUEUE
		static const bool term_queue_atomic			= true;
#else
		static const bool term_queue_atomic			= false;
#endif
		static const uintptr_t max_stack			= 0x2000;
		static const size_t stack_margin			= 0x4000;
		static const unsigned int stack_chunk_size	= 1024;

		enum gc_type_t { gc_none, gc_mark_sweep };
		static const gc_type_t gc_type				= gc_mark_sweep;
#ifdef LAMBDA_PLATFORM_MB
		static const bool workers_cpu_bound			= true;
		static const size_t macroblock_size			= (1<<max(22-(int)workers,18));
		static const int global_gc_interval_ms		= 5000;
#else
		static const bool workers_cpu_bound			= false && workers>8;
		static const size_t macroblock_size			= (1<<max(25-(int)workers,20));
		static const int global_gc_interval_ms		= 1000;
#endif

		static const int max_name_depth				= 5;
		static const lcfloat_t epsilon				;//= 0.00001;

#if defined(LAMBDA_PLATFORM_MB) || defined(LAMBDA_PLATFORM_MAC) || defined(LAMBDA_MEMCHECK) // valgrind and mudflap do not handle signals that well...
		static const bool interrupt_sleep			= false;
#else
		static const bool interrupt_sleep			= true;
#endif

#ifdef LAMBDA_TEST_ATOMIC_INDIR
		static const bool atomic_indir				= true;
#else
		static const bool atomic_indir				= false;
#endif
	};
	const lcfloat_t Config::epsilon					= 0.00001;
};

#define LAMBDA_DEFAULT_COLOR	"\x1b[37;0m"
#define LAMBDA_MAX_ARGS			5

#ifdef LAMBDA_DEBUG
#  define LAMBDA_INLINE
#else
#  define LAMBDA_INLINE __attribute__((always_inline,flatten,optimize("O3")))
#endif

#endif // __LAMBDA_CONFIG_H
