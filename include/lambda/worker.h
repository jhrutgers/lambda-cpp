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

#ifndef __LAMBDA_WORKER_H
#define __LAMBDA_WORKER_H

////////////////////////////////////
////////////////////////////////////
// Parallellism in execution
////////////////////////////////////
////////////////////////////////////

#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#ifdef LAMBDA_PLATFORM_MAC
#  include <sched.h>
#  include <sys/time.h>
typedef void* timer_t;
#endif

#include <lambda/config.h>
#include <lambda/debug.h>
#include <lambda/term.h>
#include <lambda/gc.h>
#include <lambda/vcd.h>
#include <lambda/stack.h>

namespace lambda {

	static void worker_dump_parqueuesize(int size);
	
	// optional locking for term queue, for testing purposes (default does not use locks)
	template <bool atomic=Config::term_queue_atomic>
	class TermQueueLock {
	public:
		TermQueueLock(){
			int res;
			if((res=pthread_mutex_init(&m_lock,NULL)))
				Error("Cannot initialize term queue mutex: error %d, %s",res,strerror(res));
		}
		~TermQueueLock(){
			pthread_mutex_destroy(&m_lock);
		}
		void lock(){
			int res;
			if(unlikely((res=pthread_mutex_lock(&m_lock))))
				Error("Cannot lock term queue mutex: error %d, %s",res,strerror(res));
		}
		void unlock(){
			int res;
			if(unlikely((res=pthread_mutex_unlock(&m_lock))))
				Error("Cannot unlock term queue mutex: error %d, %s",res,strerror(res));
		}
	private:
		pthread_mutex_t m_lock;
	};
	template<> class TermQueueLock<false> {
	public:
		static inline void lock(){}
		static inline void unlock(){}
	};

	// lossy queue, terms can be lost on push and doubly popped!
	template <unsigned int P>
	class TermQueue {
	public:

		static const int N=Config::term_queue_size;
		TermQueue() : m_lock(), m_top(), m_queue() {}

		void Push(Term* t,unsigned int prio=0){
			LAMBDA_ASSERT(prio<P,"invalid queue priority %u < %u",prio,P);
			m_lock[prio].lock();
			int top=m_top[prio].flush();
			if(top<N){
				m_queue[prio][top]=t;
				fence();
				m_top[prio]=top+1;
				if(prio==0)
					worker_dump_parqueuesize(top);
				LAMBDA_PRINT(queue,"push %s to %d, prio %u",t->name().c_str(),top,prio);
			}else
				LAMBDA_PRINT(queue,"queue %u overflow",prio);
			m_lock[prio].unlock();
		}
		Term* Pop(){
			for(unsigned int prio=0;prio<P;prio++){
				m_lock[prio].lock();
				int top=m_top[prio].flush();
				fence();
				if(prio==0)
					worker_dump_parqueuesize(top>0?top-1:0);
				if(top>0){
					Term* res=m_queue[prio][top-1].flush();
					m_top[prio]=top-1;
					m_lock[prio].unlock();
					return res;
				}else
					m_lock[prio].unlock();
			}
			return NULL;
		}
		void MarkActive(Stack<Term*>& more_active){
			LAMBDA_PRINT(gc_details,"marking terms on queue active...");
			// during global GC
			for(unsigned int prio=0;prio<P;prio++)
				for(int i=m_top[prio].flush()-1;i>=0;i--){
					Term* t=m_queue[prio][i].flush();
					LAMBDA_ASSERT(t!=NULL,"NULL on queue");
					LAMBDA_VALIDATE_TERM(*t);
					more_active.push(t);
				}
		}
	private:
		TermQueueLock<> m_lock[P];
		volatile_t<int>::type m_top[P];
		volatile_t<Term*>::type m_queue[P][N];
	};

	static TermQueue<2> queue;

	class Worker;

//	static __thread Worker* current_worker;
	DECL_THREAD_LOCAL_PTR(Worker*,current_worker)
#define current_worker DECL_THREAD_LOCAL_PTR_NAME(current_worker,1)

	static pthread_barrier_t worker_barrier;
	static void dot_dump_now();

	static pthread_t worker_pids[Config::workers];

	class Worker {
	public:
		Worker() : m_heap(), m_id(0), m_vcd(), m_stack_top(NULL), m_eval_stack(), m_gc_timer((timer_t)(intptr_t)-1) {
			Stats<>::Worker();
			if((m_id=workers++)){
				int res;
				pthread_attr_t attr;
				size_t def_stacksize=0,stacksize=Config::max_stack+Config::stack_margin;
#ifdef PTHREAD_STACK_MIN
				if(stacksize<PTHREAD_STACK_MIN)
					stacksize=PTHREAD_STACK_MIN;
#endif
#ifdef STACK_ALIGN
				stacksize=(stacksize+STACK_ALIGN-1)&~(STACK_ALIGN-1);
#endif
#ifdef _SC_PAGESIZE
				long sz=sysconf(_SC_PAGESIZE);
				if(sz>0)
					stacksize=(stacksize+sz-1)&~(sz-1);
#endif
				LAMBDA_ASSERT(this!=NULL,"worker without itself");
				globalize_flushmem(this,sizeof(*this));
				if(
					(res=pthread_attr_init(&attr)) ||
					(res=pthread_attr_getstacksize(&attr,&def_stacksize)) ||
					(def_stacksize<stacksize && (res=pthread_attr_setstacksize(&attr,stacksize))) ||
					(res=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE)) ||
					(res=BindCPU(&attr)) ||
					(res=pthread_create(&worker_pids[m_id],&attr,(void*(*)(void*))RunQueue,this)))
					Error("Cannot start worker %d: error %d, %s",m_id,res,strerror(res));
#ifdef LAMBDA_PLATFORM_MAC
				sched_yield();
#else
				pthread_yield();
#endif
			}else{
				current_worker=this;
				int res;
				if((res=pthread_barrier_init(&worker_barrier,NULL,Config::workers)))
					Error("Cannot initialize barrier: error %d, %s",res,strerror(res));
				StartGlobalGCTimer();
				SetAlarmHandler();
				InitHaltHandler();
				m_vcd.Init();
				m_stack_top=&res;

				pthread_attr_t attr;
				if((res=pthread_attr_init(&attr)))
					Error("Cannot check stack size: error %d, %s",res,strerror(res));
				size_t size;
				if((res=pthread_attr_getstacksize(&attr,&size)))
					Error("Cannot check stack size: error %d, %s",res,strerror(res));
				if(size<Config::max_stack+Config::stack_margin)
					Error("Stack too small: got 0x%lx, requires >= 0x%lx",size,Config::max_stack+Config::stack_margin);
				if((res=BindCPU()))
					Error("Cannot set affinity: error %d, %s",res,strerror(res));
				LAMBDA_PRINT(worker,"Worker id %d is object %p for main thread 0x%llx",Id(),this,(long long unsigned int)pthread_self());
			}
		}
		void Cleanup(){
			if(m_id==0){
				if((intptr_t)m_gc_timer!=(intptr_t)-1){
#ifdef LAMBDA_PLATFORM_MAC
					struct itimerval itv={};
					setitimer((int)(intptr_t)(m_gc_timer),&itv,NULL);
#else
					timer_delete(m_gc_timer);
#endif
				}
				for(unsigned int id=1;id<Config::workers;id++){
					int res=pthread_join(worker_pids[id],NULL);
					if(res)
						Error("Cannot join worker: error %d, %s",res,strerror(res));
				}
			}
		}
		static void RunQueue(Worker* that){
			current_worker=that;
			BlockGlobalGCTimer();
			that->SetAlarmHandler();
			LAMBDA_PRINT(worker,"Worker id %d is object %p for thread 0x%llx",that->Id(),that,(long long unsigned int)pthread_self());
			that->m_vcd.Init();
			that->Barrier();
			// first sleep, to prevent multiple workers popping all the same first term 
			usleep(2000*that->Id());

			unsigned int seed=(uintptr_t)pthread_self();
			Term_ptr stack_end=NULL;
			that->m_stack_top=&stack_end;
			Term_ptr t;
			while(true){
				switch(GetState()){
				case global_gc:
					that->InspectState(true);
					break;
				case evaluate:
					t=queue.Pop();
					if(t){
						LAMBDA_PRINT(worker,"Worker evaluates term %s",t->name().c_str());
						that->SetVCDState(VCDDump<>::evaluate);
						t->FullReduce();
//						if(Config::enable_vcd)
//							usleep(2000); // make sure evaluation is visible in vcd output
						
						that->SetVCDState(VCDDump<>::idle);
						LAMBDA_PRINT(worker,"Worker done");
						t=NULL;
						that->InspectState(true);

//						if(Config::enable_vcd)
//							usleep(10000); // easily spottable end-of-eval in the vcd output
						break;
					}// else continue to sleep
				default:
					that->InspectState(true);
					that->GetHeap().DoGC(true);
					usleep(Config::worker_idle_sleep_max/2+rand_r(&seed)%Config::worker_idle_sleep_max/2);
					that->InspectState(true);
				}
			}
		}
		static Term_tref Enqueue(Term& start,bool add_bh=true,unsigned int prio=0){
			if(Config::workers>1){
				Term_ref target=(Term&)start.FollowFullIndirection();
				Term_ptr par_target=target.term().IsReducable()&&add_bh?new Blackhole(target):(Term*)&target;
				Term_ref global_par_target=par_target->Globalize();
				queue.Push(&global_par_target,prio);
				return *target.term().SetIndirection(&global_par_target);
			}else
				return start;
		}
		lcint_t Compute(Term& start){
			lcint_t res;
			LAMBDA_PRINT(worker,"computing %p...",&start);
			SetState(evaluate);
			UnblockGlobalGCTimer();
			SetVCDState(VCDDump<>::evaluate);
			res=start.Compute<lcint_t>();
			SetVCDState(VCDDump<>::idle);
			BlockGlobalGCTimer();
			// global gc might be pending
			InspectState(true,true);
			if(GetState()==halt)
				res=128+SIGINT;
			SetState(shutdown);
			// now shutdown
			InspectState(true,true);
			return res;
		}
		int Id(){
			return m_id;
		}
		Heap<>& GetHeap(){return m_heap;}

		enum state_t { startup, evaluate, global_gc, dot_dump, halt, shutdown };
		static void SetState(state_t s){
			LAMBDA_PRINT(state,"setting system state to %d",s);
			m_state=s;
			switch(s){
			case shutdown:
			case global_gc:
				if(Config::interrupt_sleep && worker_id()==0)
					for(unsigned int i=1;i<Config::workers;i++)
						pthread_kill(worker_pids[i],SIGALRM);
			default:;
			}
		}
		static state_t GetState(){
			return m_state.flush();
		}
		bool InspectState(bool idle=false,bool main_thread=false){
			switch(GetState()){
			case startup:
			case evaluate:
				// nothing to do
				return false;
			case global_gc:
				// enter global GC now
				return GetHeap().DoGC(false);
			case dot_dump:
				LAMBDA_ASSERT(Config::enable_dot&&Config::dot_timed,"dot_dump state without dot support");
				dot_dump_now();
				SetState(evaluate);
				return false;
			case halt:
				if(!idle){
					// halt evaluation
					EvalTerm* e=GetEvalStack().top();
					if(e){
						LAMBDA_PRINT(worker,"halting");
						e->mode=EvalTerm::eval_halt;
					}
					return false;
				}else if(main_thread){
					LAMBDA_PRINT(always,"\n...interrupted...\n");
					Barrier();
					return false;
				}else{
					Barrier();
					// continue to shutdown
				}
			case shutdown:
				if(idle){
					LAMBDA_PRINT(worker,"shutdown sequence initiated");
					GetHeap().DoGC(true);
					GetHeap().DoGC(false);
					m_vcd.Close();
					Barrier();
					if(!main_thread){
						LAMBDA_PRINT(worker,"shutdown");
						Cleanup();
						pthread_exit(NULL);
					}
				}
				return false;
			default:
				Error("invalid state %d",(state_t)m_state);
			}
		}
		bool Barrier(){
			fence();
			int res=pthread_barrier_wait(&worker_barrier);
			switch(res){
			case 0:
				return false;
			case PTHREAD_BARRIER_SERIAL_THREAD:
				return true;
			default:
				Error("waiting for barrier failed: error %d, %s",res,strerror(res));
			}
		}
		VCDDump<>::state_t SetVCDState(VCDDump<>::state_t state){
			return m_vcd.SetState(state);
		}
		VCDDump<>& GetVCD(){
			return m_vcd;
		}
		void CheckStack(){
			if(m_stack_top){
				uintptr_t stack_size=0;
				if(likely((uintptr_t)&stack_size<(uintptr_t)m_stack_top))
					stack_size=(uintptr_t)m_stack_top-(uintptr_t)&stack_size;
				else
					stack_size=(uintptr_t)&stack_size-(uintptr_t)m_stack_top;

				if(stack_size>Config::max_stack){
					m_stack_top=NULL; // set to NULL to prevent retrigger by Error()
					Error("Stack overflow: using 0x%lx bytes, where 0x%lx is allowed",(unsigned long)stack_size,(unsigned long)Config::max_stack);
				}else
					LAMBDA_PRINT(mem,"Stack usage: 0x%lx bytes",(unsigned long)stack_size);
			}
		}
		Stack<EvalTerm>& GetEvalStack(){
			return m_eval_stack;
		}
		int BindCPU(pthread_attr_t* attr=NULL){
#ifndef LAMBDA_PLATFORM_MAC
			if(Config::workers_cpu_bound){
				cpu_set_t cpuset;
				CPU_ZERO(&cpuset);
				CPU_SET(Id(),&cpuset);
				if(attr)
					return pthread_attr_setaffinity_np(attr,sizeof(cpu_set_t),&cpuset);
				else
#ifdef LAMBDA_PLATFORM_MB
					return 0;
#else
					return pthread_setaffinity_np(pthread_self(),sizeof(cpu_set_t),&cpuset);
#endif
			}else
#endif
				return 0;
		}
	protected:
#ifdef LAMBDA_PLATFORM_MAC
#  define global_gc_signo SIGALRM
#else
#  define global_gc_signo (SIGRTMIN<=SIGRTMAX?SIGRTMIN:SIGUSR1)
#endif
		static void HaltHandler(int sig){
			if(GetState()==evaluate){
				SetState(halt);
			}
		}
		void InitHaltHandler(){
			int res;
			struct sigaction act={};
			act.sa_handler=HaltHandler;
			sigemptyset(&act.sa_mask);
			sigaddset(&act.sa_mask,global_gc_signo);
			if((res=sigaction(SIGINT,&act,NULL))==-1)
				Error("cannot register interrupt handler: error %d, %s",errno,strerror(errno));
		}
		static void GlobalGCHandler(int sig, siginfo_t* si,void* arg){
			if(GetState()==evaluate){
				SetState(global_gc);
			}
		}
		static void NoHandler(int sig){} // just to interrupt running usleep()s
		void SetAlarmHandler(){
			if(Config::interrupt_sleep){
				int res;
				struct sigaction act={};
				act.sa_handler=NoHandler;
				sigemptyset(&act.sa_mask);
				if((res=sigaction(SIGALRM,&act,NULL))==-1)
					Error("cannot set alarm: error %d, %s",errno,strerror(errno));
			}
		}
		void StartGlobalGCTimer(){
			if(Config::global_gc_interval_ms>0&&Config::gc_type!=Config::gc_none){
				int res=0;

				BlockGlobalGCTimer();

				struct sigaction act={};
				act.sa_flags=SA_SIGINFO;
				act.sa_sigaction=GlobalGCHandler;
				sigemptyset(&act.sa_mask);
				sigaddset(&act.sa_mask,SIGINT);
				if((res=sigaction(global_gc_signo,&act,NULL))==-1)
					Error("cannot register handler for global GC timer: error %d, %s",errno,strerror(errno));

#ifdef LAMBDA_PLATFORM_MAC
				struct itimerval itv;
				itv.it_value.tv_sec  = itv.it_interval.tv_sec  = Config::global_gc_interval_ms/1000;
				itv.it_value.tv_usec = itv.it_interval.tv_usec = (long)(Config::global_gc_interval_ms%1000)*1000L;
				if(setitimer(ITIMER_REAL,&itv,NULL)!=0)
					Error("cannot set global GC timer: error %d, %s",errno,strerror(errno));
				m_gc_timer=(timer_t)(intptr_t)ITIMER_REAL;
#else
				struct sigevent sev={};
				sev.sigev_notify=SIGEV_SIGNAL;
				sev.sigev_signo=global_gc_signo;
				if((res=timer_create(CLOCK_REALTIME,&sev,&m_gc_timer))==-1)
					Error("cannot create global GC timer: error %d, %s",errno,strerror(errno));
				
				struct itimerspec its={};
				its.it_value.tv_sec  = its.it_interval.tv_sec  = Config::global_gc_interval_ms/1000;
				its.it_value.tv_nsec = its.it_interval.tv_nsec = (long)(Config::global_gc_interval_ms%1000)*1000000L;
				if((res=timer_settime(m_gc_timer,0,&its,NULL))==-1)
					Error("cannot set global GC timer: error %d, %s",errno,strerror(errno));
#endif
			}
		}
		static void BlockGlobalGCTimer(){
			sigset_t mask;
			sigemptyset(&mask);
			sigaddset(&mask,global_gc_signo);
			int res;
			if((res=pthread_sigmask(SIG_BLOCK,&mask,NULL)))
				Error("cannot block global GC timer signal: error %d, %s",res,strerror(res));
		}
		static void UnblockGlobalGCTimer(){
			sigset_t mask;
			sigemptyset(&mask);
			sigaddset(&mask,global_gc_signo);
			int res;
			if((res=pthread_sigmask(SIG_UNBLOCK,&mask,NULL)))
				Error("cannot unblock global GC timer signal: error %d, %s",res,strerror(res));
		}
	private:
		static shared_t<state_t>::type m_state;
		static int workers;
		Heap<> m_heap ATTR_SHARED_ALIGNMENT;
		int m_id;
		VCDDump<> m_vcd;
		void* m_stack_top;
		Stack<EvalTerm> m_eval_stack;
		timer_t m_gc_timer;
	} ATTR_SHARED_ALIGNMENT;

	shared_t<Worker::state_t>::type Worker::m_state(Worker::startup);

	static Worker workers[Config::workers] __attribute__((unused)) ATTR_SHARED_ALIGNMENT;
	
	int Worker::workers=0;
	static int worker_id(){return current_worker?current_worker->Id():-1;}
	static bool gc_barrier_wait(bool reset_state){
		LAMBDA_ASSERT(current_worker!=NULL,"cannot apply barrier on non-worker");
		LAMBDA_PRINT(gc_details,"waiting for barrier...");
		bool res=current_worker->Barrier();
		if(res&&reset_state)
			current_worker->SetState(Worker::evaluate);
		return res;
	}
	static bool worker_inspect_state(){
		return current_worker->InspectState();
	}
	static bool gc_trigger_global(){
		current_worker->SetState(Worker::global_gc);
		return worker_inspect_state();
	}
	static bool worker_halt(){
		return current_worker->GetState()==Worker::halt;
	}
	static void worker_sleep(useconds_t* sleep){
		worker_inspect_state();
		// do something usefull when sleeping for some longer period
		if(!sleep||(*sleep>=Config::worker_idle_sleep_max/2&&*sleep<Config::worker_idle_sleep_max))
			current_worker->GetHeap().DoGC(true);
		// sleep now
		VCDDump<>::state_t prevvcd=worker_set_vcd(VCDDump<>::blocked);
		usleep(sleep?*sleep:Config::worker_idle_sleep_max/2);
		// wake up and resume
		if(sleep&&*sleep<Config::worker_idle_sleep_max)*sleep*=2;
		worker_set_vcd(prevvcd);
		worker_inspect_state();
	}
	static void queue_mark_active(Stack<Term*>& more_active){
		queue.MarkActive(more_active);
	}
	static Stack<EvalTerm>& worker_eval_stack(){
		return current_worker->GetEvalStack();
	}
	static VCDDump<>::state_t worker_set_vcd(VCDDump<>::state_t state){
		return current_worker->SetVCDState(state);
	}
	static void worker_dump_memusage(size_t usage){
		current_worker->GetVCD().DumpMemUsage(usage);
	}
	static void worker_dump_parqueuesize(int size){
		current_worker->GetVCD().DumpParQueueSize(size);
	}
	static void worker_dump_stdout(const char* s){
		current_worker->GetVCD().DumpStdout(current_worker->Id(),s);
	}
	static void worker_check_stack(){
		if(current_worker)
			current_worker->CheckStack();
	}

	template <typename T> __attribute__((malloc)) static void* term_alloc(size_t s){
		return current_worker->GetHeap().Alloc<T>(s);}
	static void term_free(void* p){
		current_worker->GetHeap().Free(p);}
	
	static void* noterm_alloc(size_t s) __attribute__((malloc));
	static void* noterm_alloc(size_t s){
		void* p=current_worker->GetHeap().Alloc<NoTerm>(s);
		return p;
	}
	static void noterm_free(void* p){
		current_worker->GetHeap().Free(p);}
	static void noterm_free_s(void* p,size_t) __attribute__((unused));
	static void noterm_free_s(void* p,size_t){noterm_free(p);}
	static void* noterm_realloc_s(void* p,size_t,size_t s) __attribute__((unused));
	static void* noterm_realloc_s(void* p,size_t old_s,size_t new_s){
		if(old_s>=new_s){
			return p;
		}else if(new_s==0){
			noterm_free(p);
			return NULL;
		}else if(p==NULL){
			return noterm_alloc(new_s);
		}else{
			void* res=noterm_alloc(new_s);
			memcpy(res,p,old_s);
			noterm_free(p);
			return res;
		}
	}

/*	static Term& reduction_stack_pop(){
		current_worker->ReductionStackPop();
	}
	static void reduction_stack_push(Term& t){
		current_worker->ReductionStackPush(Term& t);
	}*/
};

#endif // __LAMBDA_WORKER_H
