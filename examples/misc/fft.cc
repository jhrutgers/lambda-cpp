/*
FFT

Generates and combines sine waves, and applies FFT to it.
Usage: fft [waves]
*/
#include <lambda.h>
using namespace lambda;

FUN(twiddle,T k,T N){
	lcint_t k_i=as<lcint_t>(k);
	double N_d=(double)as<lcint_t>(N);
	return *new Constant<lccomplex_t>(cexp(-2.0*M_PI*I*k_i/N_d));
}

FUN(ditfft2,T x,T N,T s){
	let half_N =	divide (N) ((lcint_t)2);
	let X_low =		ditfft2 (x) (half_N) (mult ((lcint_t)2) (s));
	let X_high =	ditfft2 (rotate (x) (s)) (half_N) (mult ((lcint_t)2) (s));
	let k =			take (half_N) (iterate (inc) (zero));
	let tw =		map (flip (twiddle) (N)) (k);

	let recurse =	concat2
						(zipWith (add) (X_low) (zipWith (mult) (tw) (X_high)))
						(zipWith (sub) (X_low) (zipWith (mult) (tw) (X_high)));
	let stop =		take (one) (x);
	return choose (stop) (eagerList (recurse)) (eq (N) (one));
}

FUN(fft,T samples){
	let l			= length (samples);
	let input		= map (floatToComplex) (samples);
	let fft_result	= ditfft2 (input) (l) (one);
	let domain		= take (divide (l) ((lcint_t)2)) (drop (one) (fft_result));
	return eagerList (map (math_cabs) (domain));
}

FUN(wave,T sample_times,T freq){
	return map (compose (math_sin) (mult (mult ((lcfloat_t)2.0*M_PI) (freq)))) (sample_times);
}

FUN(timeframe,T sample_freq,T samples){
	let interval=divide (1.0) (sample_freq);
	return take (samples) (iterate (add (interval)) (zero_f));
}

FUN(aboveThreshold,T thr,T x){
/*	if(isReducable (thr))
		return reducedApply (flip (aboveThreshold) (x)) (thr);
	else if(isReducable (x))
		return reducedApply (aboveThreshold (thr)) (x);
	else
		return as<lcfloat_t>(x)>as<lcfloat_t>(thr)?True:False;*/
		return gt (x) (thr);
}

FUN(analyzeFFT,T sample_time,T freqs){
	let avg=divide (sum (freqs)) (intToFloat (length (freqs)));
	let thr=mult (avg) (4.0);
	return
		eagerList (
			map (flip (divide) (sample_time)) (
				map (intToFloat (inc)) (
					findIndices (aboveThreshold (thr)) (freqs)))
		);
}

FUN(generateSamples,T times,T sweep){
	return
		zipAll (add) (
		(wave (times) (sweep)) |=
		(wave (times) (2.0))   |=
		(wave (times) (9.0))   |=
		(wave (times) (12.7))  |=
		(wave (times) (18.0))  |=
		end);
}

MAIN(T args){
	lcfloat_t sample_freq=50.0;
	lcint_t sample_count=128;
	let total_time = divide (intToFloat(sample_count)) (sample_freq);
	let times = timeframe (sample_freq) (sample_count);

	let arg = choose (100) (head (args)) (isempty (args));
	let sweep = take (arg) (iterate (add (3.5)) (one_f));
	let samples = /*eagerMatrix*/ (map (compose (eagerList) (generateSamples (times))) (sweep));
	let fft_output = map (fft) (samples);
	let output = mapPar (analyzeFFT (total_time)) (fft_output);
	return
		printmatrix (output);
}

