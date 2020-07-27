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

#ifndef __LAMBDA_GC_H
#define __LAMBDA_GC_H

#include <lambda/config.h>
#include <lambda/stack.h>
#include <lambda/term.h>
#include <lambda/vcd.h>

namespace lambda {
	template <Config::gc_type_t gc=Config::gc_type> class Heap {};
	static bool gc_barrier_wait(bool reset_state=false) __attribute__((unused));
	static bool gc_trigger_global() __attribute__((unused));
	static bool worker_inspect_state() __attribute__((unused));
	static void queue_mark_active(Stack<Term*>& more_active) __attribute__((unused));
	static void dot_marked(Stack<Term*>& m) __attribute__((unused));

	class NoTerm : public Term {};

	template <>
	class Heap<Config::gc_none> {
	public:
		template <typename T> static void* Alloc(size_t s){
			return ::operator new(s);
		}
		static void Free(void* p){
			return ::operator delete(p);
		}
		static bool DoGC(bool only_local){
			return true;
		}
	};
};

#include <lambda/gc_marksweep.h>

#endif // __LAMBDA_GC_H
