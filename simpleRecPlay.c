/* ***************************************************************************
 * Paulo Pedreiras Sept 2024
 * pbrp@ua.pt
 * 
 * Example code for acquiring and processing sound using SDL
 * - There are several possibilities, set by #define directives
 * 		- Record sound and play it back
 * 		- Generate a sine wave (configurable frequency and duration)
 * 		- Add an echo (works fine for syntetich signals)
 * 		- Computing the FFT of a signal
 * 		- Debug functions (print samples, max/min, ...) * 
 * 
 * 	- All functiosn are very crude, with almost no validations. Use with care.
 * 		Bugs are almost guranteed to exist ... please report them to pbrp@ua.pt
 * 
 * Record/playback sound code adapted from:
 * 	https://gist.github.com/cpicanco/12147c60f0b62611edb1377922f443cd
 * 
 * Main SDL 2 doc.
* 	https://wiki.libsdl.org/SDL2/FrontPage
 * *************************************************************************/
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <complex.h>
#include "fft/fft.h"
#include <SDL_stdinc.h>

#define MONO 1 					/* Sample and play in mono (1 channel) */
#define SAMP_FREQ 44100			/* Sampling frequency used by audio device */
#define FORMAT AUDIO_U16		/* Format of each sample (signed, unsigned, 8,16 bits, int/float, ...) */
#define ABUFSIZE_SAMPLES 4096	/* Audio buffer size in sample FRAMES (total samples divided by channel count) */

const int MAX_RECORDING_DEVICES = 10;		/* Maximum allowed number of souns devices that will be detected */

//Maximum recording time
const int MAX_RECORDING_SECONDS = 5;

//Maximum recording time plus padding
const int RECORDING_BUFFER_SECONDS = MAX_RECORDING_SECONDS + 1;

//Receieved audio spec
SDL_AudioSpec gReceivedRecordingSpec;
SDL_AudioSpec gReceivedPlaybackSpec;

//Recording data buffer
Uint8 * gRecordingBuffer = NULL;

//Size of data buffer
Uint32 gBufferByteSize = 0;

//Position in data buffer
Uint32 gBufferBytePosition = 0;

//Maximum position in data buffer for recording
Uint32 gBufferByteMaxPosition = 0;

int gRecordingDeviceCount = 0;

char y;

/* ************************************************************** 
 * Callback issued by the capture device driver. 
 * Args are:
 * 		userdata: optional user data passed on AudioSpec struct
 * 		stream: buffer of samples acquired 
 * 		len: length of buffer ***in bytes*** (not # of samples) 
 * 
 * Just copy the data to the application buffer and update index 
 * **************************************************************/
void audioRecordingCallback(void* userdata, Uint8* stream, int len )
{
	/* Copy bytes acquired from audio stream */
	memcpy(&gRecordingBuffer[ gBufferBytePosition ], stream, len);

	/* Update buffer pointer */
	gBufferBytePosition += len;
}

/* *********************************************************************************** 
 * Callback issued by the playback device driver. 
 * Args are:
 * 		userdata: optional user data passed on AudioSpec struct
 * 		stream: buffer o samples acquired 
 * 		len: length of buffer ***in bytes*** (not # of samples) 
 * 
 * Reverse of above. Copy buffer data to the devide-river buffer and update index 
 * **********************************************************************************/
void audioPlaybackCallback(void* userdata, Uint8* stream, int len )
{
	/* Copy buffer with audio samples to stream for playback */
	memcpy(stream, &gRecordingBuffer[ gBufferBytePosition ], len);

	/* Update buffer index */
	gBufferBytePosition += len;
}


/* ***********************************************
 * Debug function: 
 *       Prints the buffer contents - 8 bit samples * 
 * **********************************************/
void printSamplesU8(uint8_t * buffer, int size) {
	int i=0;
	
	printf("\n\r Samples: \n\r");
	for(i = 0; i < size; i++) {
		printf("%3u ",buffer[i]);
		if((i%20) == 0)
			printf("\n\r");
	}		
}

/* ********************************	***************
 * Debug function: 
 *       Prints the buffer contents - uint16 samples * 
 * **********************************************/
void printSamplesU16(uint8_t * buffer, int nsamples) {
	int i=0;
	uint16_t * bufu16 = (uint16_t *)buffer;
	 
	printf("\n\r Samples: \n\r");
	for(i = 0; i < nsamples; i++) {
		printf("%5u ",bufu16[i]);
		if((i%20) == 0)
			printf("\n\r");
	}		
}

/* **************************************************
 * Audio processing example:
 *     Adds two echoes - uint16 sample format
 * 		Be carefull with saturation 
 * ************************************************* */ 
void addEchoU16(uint32_t delay1MS, uint32_t delay2MS, float gain1, float gain2, uint8_t * buffer, uint32_t nSamples)
{	
		
	int delaySamples1, delaySamples2;
	int i=0;
		
	uint16_t * procBuffer; 	/* Temporary buffer */
	uint16_t * origBuffer; 	/* Pointer to original buffer, with right sample type (UINT16 in the case) */
		
	/* Get pointer to buffer of the right type */
	origBuffer = (uint16_t *)buffer;
	
	/* Convert time to number of samples */	
	delaySamples1 = SAMP_FREQ * (float)delay1MS/1000;
	delaySamples2 = SAMP_FREQ * (float)delay2MS/1000;
	
	/* allocate temporary buffer and copy data to it*/
	procBuffer = (uint16_t *)malloc(nSamples*sizeof(uint16_t));	
	memcpy((uint8_t *)procBuffer, (uint8_t *)origBuffer, nSamples*sizeof(uint16_t));
	
	/* Add the echoes */		
	for(i = delaySamples1; i < nSamples; i++) {				
		procBuffer[i] += (uint16_t)(origBuffer[i-delaySamples1] * gain1);		
	}
	
	for(i = delaySamples2; i < nSamples; i++) {				
		procBuffer[i] +=  (uint16_t)(origBuffer[i-delaySamples2] * gain2);		
	}

	/* Move data to the original (playback) buffer */
	memcpy((uint8_t *)origBuffer, (uint8_t *)procBuffer, nSamples*sizeof(uint16_t));	
	
	/* Release resources */
	free(procBuffer);	
}

/* **************************************************************
 * Audio processing example:
 *  	Applies a low-pass filter
 * 		Args are cutoff frequency, buffer and nsamples in buffer
 * 
 * 		Simple realization derived from the discretization on an analog RC low-pass filter. See e.g. 
 * 			https://en.wikipedia.org/wiki/Low-pass_filter#Simple_infinite_impulse_response_filter 
 * ************************************************************ */ 
void filterLP(uint32_t cof, uint32_t sampleFreq, uint8_t * buffer, uint32_t nSamples)
{					
	
	int i;
	
	uint16_t * procBuffer; 	/* Temporary buffer */
	uint16_t * origBuffer; 	/* Pointer to original buffer, with right sample type (UINT16 in the case) */
	
	float alfa, beta; 
		
	/* Compute alfa and beta multipliers */
	alfa = (2 * M_PI / sampleFreq * cof ) / ( (2 * M_PI / sampleFreq * cof ) + 1 );
	beta = 1-alfa;
	
	
	/* Get pointer to buffer of the right type */
	origBuffer = (uint16_t *)buffer;
	
	/* allocate temporary buffer and init it */
	procBuffer = (uint16_t *)malloc(nSamples*sizeof(uint16_t));		
	memset(procBuffer,0, nSamples*sizeof(uint16_t));
	        
	/* Apply the filter */		
	for(i = 1; i < nSamples; i++) {				
		procBuffer[i] = alfa * origBuffer[i] + beta * procBuffer[i-1];		
	}
	
	/* Move data to the original (playback) buffer */
	memcpy(buffer, (uint8_t *)procBuffer, nSamples*sizeof(uint16_t));	
	
	/* Release resources */
	free(procBuffer);	
	
	return;
}


/* *************************************************************************************
 * Audio processing example:
 *      Generates a sine wave. 
 *      Frequency in Hz, durationMS in miliseconds, amplitude 0...0xFFFF, stream buffer 
 * 
 * *************************************************************************************/ 
void genSineU16(uint16_t freq, uint32_t durationMS, uint16_t amp, uint8_t *buffer)
{	
	int i=0, nSamples=0;
		
	float sinArgK = 2*M_PI*freq;				/* Compute once constant part of sin argument, for efficiency */
	
	uint16_t * bufU16 = (uint16_t *)buffer; 	/* UINT16 pointer to buffer for access sample by sample */
	
	nSamples = ((float)durationMS / 1000) * SAMP_FREQ; 	/* Compute how many samples to generate */
			
	/* Generate sine wave */
	for(i = 0; i < nSamples; i++) {
		bufU16[i] = amp/2*(1+sin((sinArgK*i)/SAMP_FREQ));		
	}		
	
	return;
}

/* *************************************************************************************
 * Debug function 
 *      Returns the max and min amplitude of signal in a buffer - uint16 format
 * 
 * *************************************************************************************/ 
void getMaxMinU16(uint8_t * buffer, uint32_t nSamples, uint32_t * max, uint32_t * min)
{	
	int i=0;
		
	uint16_t * origBuffer; 	/* Pointer to original buffer, with right sample type (UINT16 in the case) */
			
	/* Get pointer to buffer of the right type */
	origBuffer = (uint16_t *)buffer;
	
	/* Get max value */
	*max=origBuffer[0];
	*min=*max;
	for(i = 1; i < nSamples; i++) {		
		if(origBuffer[i] > *max)
			*max=origBuffer[i];
		if(origBuffer[i] < *min)
			*min=origBuffer[i];		
	}
	
	return;	
}


/* ***************************************
 * Main 
 * ***************************************/
int main(int argc, char ** argv)
{
	/* ****************
	 *  Variables 
	 **************** */
	SDL_AudioDeviceID recordingDeviceId = 0; 	/* Structure with ID of recording device */
	SDL_AudioDeviceID playbackDeviceId = 0; 	/* Structure with ID of playback device */
	SDL_AudioSpec desiredPlaybackSpec;			/* Structure for desired playback attributes (the ones returned may differ) */
	const char * deviceName;					/* Capture device name */
	int index;									/* Device index used to browse audio devices */
	int bytesPerSample;							/* Number of bytes each sample requires. Function of size of sample and # of channels */ 
	int bytesPerSecond;							/* Intuitive. bytes per sample sample * sampling frequency */
	
	
	/* SDL Init */
	if(SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
		return 1;
	}

	/* *************************************
	 * Get and open recording device 
	 ************************************* */
	SDL_AudioSpec desiredRecordingSpec;
	/* Defined in SDL_audio.h */
	SDL_zero(desiredRecordingSpec);				/* Init struct with default values */
	desiredRecordingSpec.freq = SAMP_FREQ;		/* Samples per second */
	desiredRecordingSpec.format = FORMAT;		/* Sampling format */
	desiredRecordingSpec.channels = MONO;		/* 1 - mono; 2 stereo */
	desiredRecordingSpec.samples = ABUFSIZE_SAMPLES;		/* Audio buffer size in sample FRAMES (total samples divided by channel count) */
	desiredRecordingSpec.callback = audioRecordingCallback;

	/* Get number of recording devices */
	gRecordingDeviceCount = SDL_GetNumAudioDevices(SDL_TRUE);		/* Argument is "iscapture": 0 to request playback device, !0 for recording device */

	if(gRecordingDeviceCount < 1)
	{
		printf( "Unable to get audio capture device! SDL Error: %s\n", SDL_GetError() );
		return 0;
	}
	
	/* and lists them */
	for(int i = 0; i < gRecordingDeviceCount; ++i)
	{
		//Get capture device name
		deviceName = SDL_GetAudioDeviceName(i, SDL_TRUE);/* Arguments are "index" and "iscapture"*/
		printf("%d - %s\n", i, deviceName);
	}

	/* If device index supplied as arg, use it, otherwise, ask the user */
	if(argc == 2) {
		index = atoi(argv[1]);		
	} else {
		/* allow the user to select the recording device */
		printf("Choose audio\n");
		scanf("%d", &index);
	}
	
	if(index < 0 || index >= gRecordingDeviceCount) {
		printf( "Invalid device ID. Must be between 0 and %d\n", gRecordingDeviceCount-1 );
		return 0;
	} else {
		printf( "Using audio capture device %d - %s\n", index, deviceName );
	}

	/* and open it */
	recordingDeviceId = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(index, SDL_TRUE), SDL_TRUE, &desiredRecordingSpec, &gReceivedRecordingSpec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
	
	/* if device failed to open terminate */
	if(recordingDeviceId == 0)
	{
		//Report error
		printf("Failed to open recording device! SDL Error: %s", SDL_GetError() );
		return 1;
	}


	/* **********************************
	 *  Get and open playback 
	 * **********************************/
	
	SDL_zero(desiredPlaybackSpec);
	desiredPlaybackSpec.freq = SAMP_FREQ;
	desiredPlaybackSpec.format = FORMAT; 
	desiredPlaybackSpec.channels = MONO;
	desiredPlaybackSpec.samples = ABUFSIZE_SAMPLES;
	desiredPlaybackSpec.callback = audioPlaybackCallback;

	/* Open playback device */
	playbackDeviceId = SDL_OpenAudioDevice( NULL, SDL_FALSE, &desiredPlaybackSpec, &gReceivedPlaybackSpec, SDL_AUDIO_ALLOW_FORMAT_CHANGE );

	/* if error abort */
	if(playbackDeviceId == 0)
	{
		//Report error
		printf("Failed to open playback device! SDL Error: %s", SDL_GetError());
		return 1;
	}


	/* **************************************************
	 * Recording and playback devices opened and OK.
	 * Time to init some data structures 
	 * **************************************************/ 
	/* Calculate number of bytes per sample */
	bytesPerSample = gReceivedRecordingSpec.channels * (SDL_AUDIO_BITSIZE(gReceivedRecordingSpec.format) / 8);

	/* Calculate number of bytes per second */
	bytesPerSecond = gReceivedRecordingSpec.freq * bytesPerSample;

	/* Calculate buffer size, for the desired duration  */
	gBufferByteSize = RECORDING_BUFFER_SECONDS * bytesPerSecond;

	/* Calculate max buffer use - some additional space to allow for extra samples*/
	/* Detection of buffer use is made form device-driver callback, so can be a biffer overrun if some */
	/* leeway is not added */ 
	gBufferByteMaxPosition = MAX_RECORDING_SECONDS * bytesPerSecond;

	/* Allocate and initialize record buffer */
	gRecordingBuffer = (uint8_t *)malloc(gBufferByteSize);
	memset(gRecordingBuffer, 0, gBufferByteSize);
	
	printf("\n\r *********** \n\r");
	printf("bytesPerSample=%d, bytesPerSecond=%d, buffer byte size=%d (allocated) buffer byte size=%d (for nominal recording)", \
			bytesPerSample, bytesPerSecond,gBufferByteSize, gBufferByteMaxPosition);
	printf("\n\r *********** \n\r");

//#define RECORD
#ifdef RECORD

	/* ******************************************************
	 * All set. Time to record, process and play sounds  
	 * ******************************************************/
	
	printf("Recording\n");
	
	/* Set index to the beginning of buffer */
	gBufferBytePosition = 0;

	/* After being open devices have callback processing blocked (paused_on active), to allow configuration without glitches */
	/* Devices must be unpaused to allow callback processing */
	SDL_PauseAudioDevice(recordingDeviceId, SDL_FALSE ); /* Args are SDL device id and pause_on */
	
	/* Wait until recording buffer full */
	while(1)
	{
		/* Lock callback. Prevents the following code to not concur with callback function */
		SDL_LockAudioDevice(recordingDeviceId);

		/* Receiving buffer full? */
		if(gBufferBytePosition > gBufferByteMaxPosition)
		{
			/* Stop recording audio */
			SDL_PauseAudioDevice(recordingDeviceId, SDL_TRUE );
			SDL_UnlockAudioDevice(recordingDeviceId );
			break;
		}

		/* Buffer not yet full? Keep trying ... */
		SDL_UnlockAudioDevice( recordingDeviceId );
	}

	/* *****************************************************************
	 * Recorded data obtained. Now process it and play it back
	 * *****************************************************************/
 
#endif

#define GENSINE
#ifdef GENSINE
	printf("\n Generating a sine wave \n");
	genSineU16(1000, 1000, 30000, gRecordingBuffer); 	/* freq, durationMS, amp, buffer */
#endif

//#define ADDECHO
#ifdef ADDECHO
	printf("Adding an echo \n");
	addEchoU16(1000, 2000, 0.2, 0.1,gRecordingBuffer,gBufferByteMaxPosition/sizeof(uint16_t)); /* (float delay1MS, float delay2MS, float gain1, float gain2, uint8 * buffer, uint32_t nSamples) */
#endif

	/* For debug - if you want to check the data. */
	/* Can be usefull to add a function that dumps the data to a file for processing e.g. in Matlab/Octave */
//	printSamplesU16(gRecordingBuffer,4000); /* Args are pointer to buffer and nsamples (not bytes)	*/		
	
	/* For debug															*/ 
	/* Getting max-min can be usefull to detect possible saturation 		*/
	{
		uint32_t max, min;
		getMaxMinU16(gRecordingBuffer,gBufferByteMaxPosition/sizeof(uint16_t), &max, &min);
		printf("Maxa mplitude: = %u Min amplitude is:%u\n",max, min);
	}


//#define MAXMIN
#ifdef MAXMIN	
	{
		uint32_t max, min;
		getMaxMinU16(gRecordingBuffer, gBufferByteMaxPosition/sizeof(uint16_t), &max, &min);  // getMaxMinU16(uint8_t * buffer, uint32_t nSamplesm, uint32_t max, uint32_t min)
		printf("\n Max amplitude = %u Min amplitude = %u\n",max, min);
	}
#endif	

#define LPFILTER
#ifdef LPFILTER
	/* Apply LP filter */
	/* Args are cutoff freq, sampling freq, buffer and # of samples in the buffer */
	printf("\n Applying LP filter \n");
	filterLP(1000, SAMP_FREQ, gRecordingBuffer, gBufferByteMaxPosition/sizeof(uint16_t)); 
#endif

#define FFT
#ifdef FFT
	{		
		int N=0;	// Number of samples to take
		int sampleDurationMS = 100; /* Duration of the sample to analyze, in ms */
		int k=0; 	// General counter
		uint16_t * sampleVector = (uint16_t *)gRecordingBuffer; // Vector of samples 
		float * fk; /* Pointer to array with frequencies */
		float * Ak; /* Pointer to array with amplitude for frequency fk[i] */
		complex double * x; /* Pointer to array of complex values for samples and FFT */
	
		printf("\nComputing FFT of signal\n");
		
		/* Get the vector size, making sure it is a power of two */
		for(N=1; pow(2,N) < (SAMP_FREQ*sampleDurationMS)/1000; N++);
		N--;
		N=(int)pow(2,N);
		
		printf("# of samples is: %d\n",N);
		
		/* Allocate memory for  samples, frequency and amplitude vectors */
		x = (complex double *)malloc(N * sizeof(complex double)); /* Array for samples and FFT output */
		fk = (float *)malloc(N * sizeof(float)); 	/* Array with frequencies */
		Ak = (float *)malloc(N * sizeof(float)); 	/* Array with amplitude for frequency fk[i] */
				
		/* Copy samples to complex input vector */
		for(k=0; k<N;k++) {
			x[k] = sampleVector[k];
		}
				
		/* Compute FFT */
		fftCompute(x, N);

		//printf("\nFFT result:\n");/		
		//printComplexArray(x, N);
    
		/* Compute the amplitude at each frequency and print it */
		fftGetAmplitude(x,N,SAMP_FREQ, fk,Ak);

		for(k=0; k<=N/2; k++) {
			printf("Amplitude at frequency %f Hz is %f \n", fk[k], Ak[k]);
		}
	}
	
#endif
	
	/* *****************************************************************
	 * Recorded/generated data obtained. Now play it back
	 * *****************************************************************/
	printf("Playback\n");
		
	/* Reset buffer index to the beginning */
	gBufferBytePosition = 0;
	
	/* Enable processing of callbacks by playback device (required after opening) */
	SDL_PauseAudioDevice(playbackDeviceId, SDL_FALSE);

	/* Play buffer */
	while(1)
	{
		/* Lock callback */
		SDL_LockAudioDevice(playbackDeviceId);

		/* Playback is finished? */
		if(gBufferBytePosition > gBufferByteMaxPosition)
		{
			/* Stop playing audio */
			SDL_PauseAudioDevice(playbackDeviceId, SDL_TRUE);
			SDL_UnlockAudioDevice(playbackDeviceId);	
			break;
		}

		/* Unlock callback and try again ...*/
		SDL_UnlockAudioDevice(playbackDeviceId);
	}

	/* *******************************************
	 * All done! Release resources and terminate
	 * *******************************************/
	if( gRecordingBuffer != NULL )
	{
		free(gRecordingBuffer);
		gRecordingBuffer = NULL;
	}

	SDL_Quit();
	
	return 0;
}
