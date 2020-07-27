/*
Matrix inverse
*/
#include <lambda.h>
using namespace lambda;

MAIN(T args){
	let m =
		(2.0 |=  1.0 |=  0.0 |=  -0.1 |= end) |=
		(5.0 |=  1.0 |=  3.3 |=   0.0 |= end) |=
		(1.7 |= 11.9 |= -6.2 |= -3.14 |= end) |=
		(9.3 |=  0.0 |=  0.0 |=   4.9 |= end) |= end;

	return
		printmatrix (m),
		printmatrix (m_inv (m));
}

