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

#ifndef __LAMBDA_VCD_H
#define __LAMBDA_VCD_H

#include <stdio.h>
#include <sys/time.h>
#include <errno.h>

#include <lambda/config.h>
#include <lambda/debug.h>

#ifndef LAMBDA_VCD_PREFIX
#  define LAMBDA_VCD_PREFIX lambda
#endif

namespace lambda {
	
	template <bool enabled=Config::enable_vcd>
	class VCDDump {
	public:
		enum state_t { startup='X', idle='0', evaluate='1', blocked='z', local_gc='w', global_gc='-' };
		void Init(){}
		void Close(){}
		state_t SetState(state_t state){return idle;}
		void DumpMemUsage(size_t bytes){}
		void DumpParQueueSize(int count){}
		void DumpStdout(int worker,const char* s){}
	};
	
	template <>
	class VCDDump<true> : public VCDDump<false> {
	public:
		VCDDump() : m_cur(startup), m_id(-1), m_fp(NULL) {}
		void Init(){
			char* filename;
			m_id=worker_id();
			if(asprintf(&filename,STRINGIFY(LAMBDA_VCD_PREFIX) "_w%d.vcd",m_id)==-1)
				Error("cannot open vcd file");
			if((m_fp=fopen(filename,"w"))==NULL)
				Error("cannot open vcd file %s: error %d, %s",filename,errno,strerror(errno));
			if(fprintf(m_fp,"$var reg 1 w%d worker%d $end\n",m_id,m_id)<0)
				Error("cannot write header of vcd file %s",filename);
			free(filename);
			SetState(idle);
		}
		void Close(){
			if(m_fp){
				SetState(idle);
				fclose(m_fp);
				m_fp=NULL;
			}
		}
		static long long unsigned int MakeTimestamp(struct timeval* tv){
			return (long long unsigned int)tv->tv_sec*1000L+tv->tv_usec/1000;
		}
		state_t SetState(state_t state){
			struct timeval tv;
			if(m_fp&&!gettimeofday(&tv,NULL))
				fprintf(m_fp,"#%llu %cw%d\n",MakeTimestamp(&tv),(char)state,m_id);
			state_t old=m_cur;
			m_cur=state;
			return old;
		}
		void DumpMemUsage(size_t bytes){
			struct timeval tv;
			if(m_fp&&!gettimeofday(&tv,NULL))
				fprintf(m_fp,"#%llu r%lu m\n",MakeTimestamp(&tv),bytes/1024);
		}
		void DumpParQueueSize(int count){
			struct timeval tv;
			if(m_fp&&!gettimeofday(&tv,NULL))
				fprintf(m_fp,"#%llu r%d p\n",MakeTimestamp(&tv),count);
		}
		void DumpStdout(int worker,const char* s){
			struct timeval tv;
			if(m_fp&&!gettimeofday(&tv,NULL)){
				char* s2;
				if(asprintf(&s2,"w%d: %s",worker,s)!=-1){
					for(char* p=s2;*p;p++)
						if(*p=='\n')*p='\t';
						else if(*p<32||*p>126)*p=' ';
					fprintf(m_fp,"#%llu s%s s\n",MakeTimestamp(&tv),s2);
					free(s2);
				}
			}
		}
	private:
		state_t m_cur;
		int m_id;
		FILE* m_fp;
	};
	
	// see worker.h
	static VCDDump<>::state_t worker_set_vcd(VCDDump<>::state_t state) __attribute__((unused));
};

#endif // __LAMBDA_VCD_H
