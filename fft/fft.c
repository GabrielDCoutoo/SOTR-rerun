
/* ************************************************************
 * Paulo Pedreiras, pbrp@ua.pt
 * 2024/Sept
 * 
 * C module to compute the Fast Fourier Transform (FFT) of an array
 * using the (recursive) Cooley-Tukey algorithm.  
 
 * ************************************************************/

#ifndef FFT_H
#define FFT_H

#include <stdio.h>
#include <math.h>
#include <complex.h>
#include "fft.h"

/* *******************************************************************
 * Function to perform the FFT (recursive version) 
 * *******************************************************************/
void fftCompute(complex double *X, int N) {
    
    if (N <= 1) return;  // Base case: FFT of size 1 is the same

    // Split even and odd terms
    complex double even[N/2];
    complex double odd[N/2];

    for (int i = 0; i < N/2; i++) {
        even[i] = X[i * 2];
        odd[i] = X[i * 2 + 1];
    }

    // Recursively apply FFT to even and odd terms
    fftCompute(even, N/2);
    fftCompute(odd, N/2);

    // Combine results
    for (int k = 0; k < N/2; k++) {
        complex double t = cexp(-2.0 * I * M_PI * k / N) * odd[k];
        X[k]       = even[k] + t;
        X[k + N/2] = even[k] - t;
    }
}

/* **********************************************************
 *  Converts complex representation of FFT in amplitudes.
 *  Also generates the corresponding frequencies 
 * **********************************************************/
void fftGetAmplitude(complex double * X, int N, int fs, float * fk, float * Ak) {
    
    int k=0;
    
    /* Compute freqs: from 0/DC to fs, obver the N bins */
    /* Output vector is mirrored, so only the first N/2 bins are relevant */
    for(k=0; k<=N/2; k++) {
		fk[k]=k*fs/N;		
//		printf("fk[%d]=%f\n",k,fk[k]);
	}
    
    /* Compute amplitudes */
    Ak[0] = 1.0/N*cabsf(X[0]);
    Ak[N/2] = 1.0/N*cabsf(X[N/2]);
    for(k=1; k<N/2; k++) {
		Ak[k] = 2.0/N*cabsf(X[k]);
	}
	
//	 for(k=0; k<=N/2; k++) {	
//		printf(">>fk[%d]=%f\n",k,fk[k]);
//	}
	
	return;    
}

/* ******************************************************
 *  Helper function to print complex arrays
  * ******************************************************/
void printComplexArray(complex double *X, int N) {
    for (int i = 0; i < N; i++) {
        printf("%g + %gi\n", creal(X[i]), cimag(X[i]));
    }
}

#endif