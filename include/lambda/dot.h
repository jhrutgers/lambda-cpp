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

#ifndef __LAMBDA_DOT_H
#define __LAMBDA_DOT_H

#include <lambda/config.h>
#include <lambda/term.h>

#ifndef LAMBDA_DOT_FILE
#  define LAMBDA_DOT_FILE lambda.dot
#endif

namespace lambda {

	template <bool enabled=Config::enable_dot>
	class DotDump {
#ifndef LAMBDA_PLATFORM_MAC
	public:
		DotDump() : m_terms(), m_subterms(), m_graphs(-100), m_graphs_skip(1), m_timed(NULL), m_timer(0), m_marked(), m_fp(NULL) {
			LAMBDA_ASSERT(Config::workers==1||(!Config::dot_all&&!Config::dot_timed),"cannot have multiple workers and dot output");
			LAMBDA_ASSERT(Config::dot_all==false||Config::dot_timed==false,"cannot enable both Config::dot_all and Config::dot_timed");
			int res;
			if((res=pthread_mutex_init(&m_fp_lock,NULL)))
				Error("cannot initialize dot mutex: error %d, %s",res,strerror(res));
			if(Config::workers==1){
				// open and truncate once, and stay open
				m_fp=Open(false);
				if(Config::dot_timed)
					StartTimer();
			}else{
				// truncate file
				Close(Open(false));
			}
		}
		~DotDump(){
			Close(m_fp);
			pthread_mutex_destroy(&m_fp_lock);
			if(Config::dot_timed)
				timer_delete(m_timer);
		}
		void dump(Term& t,Term& mark){
			if(m_graphs<0||m_graphs==(1<<m_graphs_skip)){
				dump(t,&mark);
				if(m_graphs>0&&m_graphs_skip<0x1000000){
					m_graphs_skip++;
					m_graphs=0;
				}
				if(m_graphs_skip>20&&m_fp)
					fflush(m_fp);
			}
			m_graphs++;
		}
		void dump(Term& t,Term* mark=NULL){
			FILE* fp=m_fp;
			if(!fp)fp=Open();
			fprintf(fp,"strict digraph lambda_%d {\n",m_graphs<0?0:m_graphs);
			dumpTerm(t,fp);
			for(Term** p=m_marked;p<&m_marked[sizeof(m_marked)/sizeof(*m_marked)]&&*p;p++){
				fprintf(fp,"%s [style=filled,fillcolor=yellow];\n",(*p)->DotID().c_str());
				dumpTerm(**p,fp);
			}
			if(mark)
				fprintf(fp,"%s [style=filled,fillcolor=green];\n",mark->DotID().c_str());
			fprintf(fp,"};\n");
			if(!m_fp)Close(fp);
		}
		void timed(Term& t){
			if(Config::dot_timed){
				LAMBDA_PRINT(code,"setting timed dot for %s",t.name().c_str());
				m_timed=&t;
				mark(t);
				dumpNow();
			}
		}
		void mark(Term& t){
			Term** p=m_marked;
			while(*p&&p<&m_marked[sizeof(m_marked)/sizeof(*m_marked)-1])p++;
			LAMBDA_PRINT(code,"marking #%lu for dot: %s",((uintptr_t)p-(uintptr_t)m_marked)/sizeof(*m_marked),t.name().c_str());
			*p=&t;
		}
		void marked(Stack<Term*>& m){
			for(Term** p=m_marked;p<&m_marked[sizeof(m_marked)/sizeof(*m_marked)]&&*p;p++)
				m.push(*p);
		}
		void dumpNow(){
			if(m_timed){
				dump(*m_timed);
				if(m_fp)
					fflush(m_fp);
			}
		}
	protected:
		void dumpTerm(Term& t,FILE* fp){
			LAMBDA_ASSERT(fp!=NULL,"invalid dot file");
			Term* p=&t;
			while(p){
				fprintf(fp,"%s [label=\"%s\"];\n",p->DotID().c_str(),p->DotName().c_str());
				p->DotFollow(m_subterms);
				Term* subp;
				while((subp=m_subterms.pop())){
					fprintf(fp,"%s -> %s;\n",p->DotID().c_str(),subp->DotID().c_str());
					m_terms.push(subp);
				}
				p=m_terms.pop();
			}
		}
		static const int timer_signo=SIGUSR2;
		static void DotTimerHandler(int sig, siginfo_t* si,void* arg){
			if(Worker::GetState()==Worker::evaluate)
				Worker::SetState(Worker::dot_dump);
//			((DotDump*)si->si_value.sival_ptr)->dumpNow();
		}
		void StartTimer(){
			int res=0;

			struct sigaction act;
			memset(&act,0,sizeof(act));
			act.sa_flags=SA_SIGINFO;
			act.sa_sigaction=DotTimerHandler;
			sigemptyset(&act.sa_mask);
			if((res=sigaction(timer_signo,&act,NULL))==-1)
				Error("cannot register handler for dot timer: error %d, %s",errno,strerror(errno));
			struct sigevent sev;
			memset(&sev,0,sizeof(sev));
			sev.sigev_notify=SIGEV_SIGNAL;
			sev.sigev_signo=timer_signo;
			sev.sigev_value.sival_ptr=this;
			if((res=timer_create(CLOCK_REALTIME,&sev,&m_timer))==-1)
				Error("cannot create dot timer: error %d, %s",errno,strerror(errno));
			
			struct itimerspec its;
			memset(&its,0,sizeof(its));
			its.it_value.tv_sec  = its.it_interval.tv_sec  = Config::dot_timed_interval_ms/1000;
			its.it_value.tv_nsec = its.it_interval.tv_nsec = (long)(Config::dot_timed_interval_ms%1000)*1000000L;
			if((res=timer_settime(m_timer,0,&its,NULL))==-1)
				Error("cannot set dot timer: error %d, %s",errno,strerror(errno));
		}
		FILE* Open(bool append=true){
			int res;
			if((res=pthread_mutex_lock(&m_fp_lock)))
				Error("cannot lock dot mutex: error %d, %s",res,strerror(res));
			FILE* fp;
			if(!(fp=fopen(STRINGIFY(LAMBDA_DOT_FILE),append?"a":"w")))
				Error("cannot open dot file: error %d, %s",errno,strerror(errno));
			return fp;
		}
		void Close(FILE* fp){
			if(fp)
				fclose(fp);
			int res;
			if((res=pthread_mutex_unlock(&m_fp_lock)))
				Error("cannot unlock dot mutex: error %d, %s",res,strerror(res));
		}
	private:
		Stack<Term*> m_terms;
		Stack<Term*> m_subterms;
		int m_graphs;
		int m_graphs_skip;
		Term* m_timed;
		timer_t m_timer;
		Term* m_marked[16];
		FILE* m_fp;
		pthread_mutex_t m_fp_lock;
	};

	template <>
	class DotDump<false> {
#endif // LAMBDA_PLATFORM_MAC
	public:
		void dump(Term& t,Term& mark){}
		void dump(Term& t,Term* mark=NULL){}
		void dumpNow(){}
		void timed(Term& t){}
		void mark(Term& t){}
		void marked(Stack<Term*>& m){}
	};

	DotDump<> dotdump;

	static void dot_dump(Term& t,Term& mark){
		dotdump.dump(t,mark);
	}
	static void dot_dump_now(){
		dotdump.dumpNow();
	}
	static void dot_timed(Term& t) __attribute__((unused));
	static void dot_timed(Term& t) {
		dotdump.timed(t);
	}
	static void dot_mark(Term& t) __attribute__((unused));
	static void dot_mark(Term& t){
		dotdump.mark(t);
	}
	static void dot_marked(Stack<Term*>& m){
		dotdump.marked(m);
	}
};

#endif // __LAMBDA_DOT_H
