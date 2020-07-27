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

#ifndef __LAMBDA_STATS_H
#define __LAMBDA_STATS_H

#include <stdio.h>

#include <lambda/config.h>
#include <lambda/debug.h>

namespace lambda {

	static void worker_dump_memusage(size_t usage);

	template <bool enable=Config::enable_stats> class Stats {};

	template <>
	class Stats<true> {
	public:
		static void LocalTerm(){AtomicInc(&s.locals);}
		static void GlobalizedTerm(){AtomicInc(&s.globals);}
		static void Application(){AtomicInc(&s.applications);}
		static void Stall(){AtomicInc(&s.stalls);}
		static void Double(){AtomicInc(&s.doubles);}
		static void Postponed(){AtomicInc(&s.postponed);}
		static void Worker(){AtomicInc(&s.workers);}
		static void Macroblock(){worker_dump_memusage(AtomicInc(&s.macroblocks)*Config::macroblock_size);}
		static void Print(){
			print_lock();
			printf("Stats:\n"
				"    locals      : %10llu\n"
				"    globals     : %10llu\n"
				"    applications: %10llu\n"
				"    stalls      : %10llu\n"
				"    doubles     : %10llu\n"
				"    postponed   : %10llu\n"
				"    workers     : %10llu\n"
				"    macroblocks : %10llu (%llu KB)\n",
				(unsigned long long int)s.locals,(unsigned long long int)s.globals,(unsigned long long int)s.applications,
				(unsigned long long int)s.stalls,(unsigned long long int)s.doubles,(unsigned long long int)s.postponed,(unsigned long long int)s.workers,
				(unsigned long long int)s.macroblocks,(unsigned long long int)s.macroblocks*Config::macroblock_size/1024
				);
			print_unlock();
		}
	protected:
		template <typename T> static T AtomicInc(T* t){
			return atomic_add(const_cast<typename lambda::cv_type<typeof(*t)>::type_nc*>(t),1ULL);
		}
	private:
		typedef struct {
			shared_t<unsigned long long int>::type locals,globals,applications,stalls,doubles,postponed,workers,macroblocks;
		} s_t;
		static s_t s;
	};
	
	template <>
	class Stats<false> {
	public:
		static void LocalTerm(){}
		static void GlobalizedTerm(){}
		static void Application(){}
		static void Stall(){}
		static void Double(){}
		static void Postponed(){}
		static void Worker(){}
		static void Print(){}
		static void Macroblock(){}
	private:
		typedef int s_t[0];
		static s_t s;
	};
	
	Stats<Config::enable_stats>::s_t Stats<Config::enable_stats>::s __attribute__((unused)) ATTR_SHARED_ALIGNMENT ={};

};

#endif // __LAMBDA_STATS_H
