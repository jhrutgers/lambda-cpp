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

#ifdef __APPLE__
#ifndef PTHREAD_BARRIER_H_
#define PTHREAD_BARRIER_H_

#include <pthread.h>
#include <errno.h>

#define PTHREAD_BARRIER_SERIAL_THREAD -2

typedef int pthread_barrierattr_t;
typedef struct
{
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int count;
	int tripCount;
} pthread_barrier_t;

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count){
	if(count == 0)
		return EINVAL;
	int res=0;
	if(	(res=pthread_mutex_init(&barrier->mutex, 0)) || 
		(res=pthread_cond_init(&barrier->cond, 0)))
		return res;
	
	barrier->tripCount = count;
	barrier->count = 0;
	return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier){
	pthread_cond_destroy(&barrier->cond);
	pthread_mutex_destroy(&barrier->mutex);
	return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier){
	int res=0;
	if((res=pthread_mutex_lock(&barrier->mutex)))
		return res;
	
	barrier->count++;

	if(barrier->count >= barrier->tripCount){
		barrier->count = 0;
		(res=pthread_cond_broadcast(&barrier->cond)) ||
		(res=pthread_mutex_unlock(&barrier->mutex)) || 
		(res=PTHREAD_BARRIER_SERIAL_THREAD);
		return res;
	}
	else
	{
		(res=pthread_cond_wait(&barrier->cond, &(barrier->mutex))) ||
		(res=pthread_mutex_unlock(&barrier->mutex));
		return res;
	}
}

#endif // PTHREAD_BARRIER_H_
#endif // __APPLE__, else just use native pthread calls
