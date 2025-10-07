/* ************************************************************
 * Paulo Pedreiras, pbrp@ua.pt
 * 2024/Sept
 * 
 * C module to compute the Fast Fourier Transform (FFT) of an array
 * using the (recursive) Cooley-Tukey algorithm. 
 * Requires that the number of elements is a power of 2.
 * 
 * The function takes as input and returns an array of complex numbers.
 * The input array has real numbers - the samples
 * The output is an array of complex numbers that represent the frequency 
 *    components of the original signal.
 * Note that the FFT output array is mirrored and gives the frequency 
 *    components from 0 Hz to fs/2 as complex numbers and in N bins. 
 *    Bin [0] 1 is DC and bin [N/2-1] is fs/2. Note also that the 
 *    magnitudes must be scaled by 2/N, except for Dc and fs/2 which 
 *    is 1/N (the others are doubled because of mirroring)
 
 * ************************************************************/

#include <math.h>
#include <complex.h>

/* *******************************************************************
 * Function to perform the FFT (recursive version)
 * Args are:
 * 		complex double X: the input (real values) and output 
 *                    frequency component ampliutude/phase
 * 		int N: the number of elements. *** MUST BE A POWER OF 2 ****
 * *******************************************************************/
void fftCompute(complex double *X, int N);

/* **********************************************************
 *  Converts complex representation of FFT in amplitudes.
 *  Also generates the corresponding frequencies
 * 	Args:
 * 		complex double X: array of complex numbers
 * 		int N: length of the array
 * 		int fs: sampling frequency (in Hz)
 * 		float *fk: vector were frequencies will be placed
 * 		float *Ak: vector for amplitudes at each frequency
 * **********************************************************/
void fftGetAmplitude(complex double * X, int N, int fs, float * fk, float * Ak);

/* ******************************************************
 *  Helper function to print complex arrays
 * 	Args:
 * 		complex double X: array of complex numbers
 * 		int N: length of the array
 * ******************************************************/
void printComplexArray(complex double *X, int N);
