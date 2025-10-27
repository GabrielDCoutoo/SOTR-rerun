#define _GNU_SOURCE /* Must precede #include <sched.h> for sched_setaffinity */ 
#define __USE_MISC
#define ABUFSIZE_SAMPLES 4096
#define NTASKS 7
#include "rtsounds.h"
#include <math.h>
#include <complex.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SDL.h>
#include <semaphore.h>
#include <time.h>    // added for timestamped filenames

struct timespec TsAdd(struct timespec ts1, struct timespec ts2) {
    struct timespec tr;
    tr.tv_sec = ts1.tv_sec + ts2.tv_sec;
    tr.tv_nsec = ts1.tv_nsec + ts2.tv_nsec;
    if (tr.tv_nsec >= NS_IN_SEC) {
        tr.tv_sec++;
        tr.tv_nsec -= NS_IN_SEC;
    }
    return tr;
}

void save_audio_to_wav(const char* filename, Uint8* buffer, Uint32 buffer_size, int sample_rate);

/* ***********************************************
* Global variables
* ***********************************************/
cab cab_buffer;
SDL_AudioDeviceID recordingDeviceId = 0;  
Uint8 *gRecordingBuffer = NULL;
SDL_AudioSpec gReceivedRecordingSpec;
Uint32 gBufferBytePosition = 0, gBufferByteMaxPosition = 0, gBufferByteSize = 0;

int gBytesPerSample = 0; // <-- new: bytes per audio frame (channels * bytes per sample)

volatile float detectedSpeedFrequency = 0.0, maxAmplitudeDetected = 0.0;

pthread_mutex_t updatedVarMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t updatedVar = PTHREAD_COND_INITIALIZER;
sem_t data_ready;  // Semaphore for managing data readines

FILE *gantt_logf;  // For Gantt data (gantt_log.csv)
FILE *status_logf; // For Status reports (rtsounds_log.txt)
pthread_mutex_t ganttLogMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t statusLogMutex = PTHREAD_MUTEX_INITIALIZER;
/* *************************
* Thread Functions
* *************************/


void* Audio_thread(void* arg) {
    // A thread de áudio da SDL é o verdadeiro produtor, chamando o callback.
    // Esta thread serve apenas para iniciar a gravação e manter o RT agendamento.
    
    // Obter a prioridade para a mensagem de debug
    int prio = ((struct sched_param*)arg)->sched_priority; 
    printf("Audio Thread (Produtor) Running - Prio: %d\n", prio);
    
    // Inicia a gravação de áudio de forma contínua. 
    // Isto faz com que a audioRecordingCallback seja chamada continuamente.
    SDL_PauseAudioDevice(recordingDeviceId, SDL_FALSE); 

    // O trabalho pesado é delegado ao callback. Esta thread principal deve apenas esperar.
    while (1) {
        // Usa SDL_Delay para manter a thread viva sem consumir CPU excessivo
        // (Embora não seja estritamente necessário num modelo RT em que outras threads correm)
        SDL_Delay(1000); 
    }
    return NULL;
}
// rtsounds.c
// rtsounds.c
// rtsounds.c

void* Speed_thread(void* arg) {
    const int N = ABUFSIZE_SAMPLES; 
    complex double x[N]; 
    float fk[N];
    float Ak[N];

    struct timespec period = {0, 200000000}; // 200ms
    struct timespec next_wakeup;

    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority; 
    printf("Speed Thread Running - Prio: %d\n", prio);

    while (1) {
        next_wakeup = TsAdd(next_wakeup, period);

        // --- GANTT: CAPTURE START TIME ---
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        // --- END GANTT ---

        printf("DEBUG SPEED: Dados recebidos! A processar...\n"); 
        
        buffer* readBuffer = cab_getReadBuffer(&cab_buffer); 

        if (readBuffer != NULL) {
            
            filterLP(COF, SAMP_FREQ, (uint8_t*)readBuffer->buf, N);

            for (int k = 0; k < N; k++) {
                double centered_sample = (double)readBuffer->buf[k] - 32768.0; 
                x[k] = centered_sample + 0.0 * I;
            }
            
            cab_releaseReadBuffer(&cab_buffer, readBuffer->index); 

            fftCompute(x, N);
            fftGetAmplitude(x, N, SAMP_FREQ, fk, Ak);
            
            float maxA = 0.0;
            float maxF = 0.0;
            const float MAX_FREQ_TO_CHECK = COF + 50.0;

            for(int k=1; k<=N/2; k++) {
                if (Ak[k] > maxA && fk[k] < MAX_FREQ_TO_CHECK) {
                    maxA = Ak[k];
                    maxF = fk[k];
                }
            }
            
            pthread_mutex_lock(&updatedVarMutex);
            detectedSpeedFrequency = maxF;
            maxAmplitudeDetected = maxA;
            pthread_mutex_unlock(&updatedVarMutex);
            
            printf("DEBUG SPEED: Max Freq=%.2f Hz, Max Amp=%.2f (Loop concluído)\n", maxF, maxA);
        }
        
        // --- GANTT: CAPTURE END TIME & LOG ---
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        pthread_mutex_lock(&ganttLogMutex); // Use GANTT mutex
        gantt_logf = fopen("gantt_log.csv", "a"); // Log to CSV
        if (gantt_logf) {
            fprintf(gantt_logf, "GANTT,Speed_thread,%d,%ld,%ld,%ld,%ld\n", 
                    prio, start_time.tv_sec, start_time.tv_nsec, 
                    end_time.tv_sec, end_time.tv_nsec);
            fclose(gantt_logf);
        }
        pthread_mutex_unlock(&ganttLogMutex);
        // --- END GANTT ---
        
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL); 
    }
    return NULL;
}
// Adicionar ao lado das outras thread functions
// rtsounds.c

void* Display_thread(void* arg) {
    struct timespec period = {5, 0}; // 5 seconds
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("Display Thread Running - Prio: %d, Period: 5s\n", prio);

    while (1) {
        next_wakeup = TsAdd(next_wakeup, period);
        
        // --- GANTT: CAPTURE START TIME ---
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        // --- END GANTT ---

        float speedFreq = 0.0;
        float maxAmp = 0.0;
        int isIssue = 0;
        int direction = 0; 
        float issueR = 0.0;

        pthread_mutex_lock(&updatedVarMutex);
        speedFreq = detectedSpeedFrequency;
        maxAmp = maxAmplitudeDetected;
        isIssue = issueDetected; 
        direction = directionValue; 
        issueR = issueRatio;
        pthread_mutex_unlock(&updatedVarMutex);

        const char *issueStatus = isIssue ? "FAULT DETECTED! (High Prio 60)" : "OK";
        const char *dirStatus = (direction == 1) ? "FORWARD (Accelerating)" : 
                                (direction == -1) ? "REVERSE (Decelerating)" : 
                                (direction == 2) ? "STABLE (Constant Speed)" :
                                "STOPPED";
        
        // --- Print to Console ---
        printf("===========================================\n");
        printf(" REAL-TIME MONITORING SYSTEM (Prio %d)\n", prio);
        printf("===========================================\n");
        printf(" [TASK STATUS]\n");
        printf(" SPEED (Prio 40): \t%.2f Hz\n", speedFreq);
        printf(" ISSUE (Prio 60): \t%s\n", issueStatus);
        printf(" DIRECTION (Prio 50): \t%s\n", dirStatus);
        printf("===========================================\n");
        printf(" [DEBUG]\n");
        printf(" Speed Thread Max Amplitude: \t%.2f\n", maxAmp);
        printf(" Issue Thread Ratio: \t\t%.2f\n", issueR); 
        printf("===========================================\n\n");

        // --- Print to Status Log File (rtsounds_log.txt) ---
        pthread_mutex_lock(&statusLogMutex);
        status_logf = fopen("rtsounds_log.txt", "a");
        if (status_logf) {
            fprintf(status_logf, "===========================================\n");
            fprintf(status_logf, " REAL-TIME MONITORING SYSTEM (Prio %d)\n", prio);
            fprintf(status_logf, "===========================================\n");
            fprintf(status_logf, " [TASK STATUS]\n");
            fprintf(status_logf, " SPEED (Prio 40): \t%.2f Hz\n", speedFreq);
            fprintf(status_logf, " ISSUE (Prio 60): \t%s\n", issueStatus);
            fprintf(status_logf, " DIRECTION (Prio 50): \t%s\n", dirStatus);
            fprintf(status_logf, "===========================================\n");
            fprintf(status_logf, " [DEBUG]\n");
            fprintf(status_logf, " Speed Thread Max Amplitude: \t%.2f\n", maxAmp);
            fprintf(status_logf, " Issue Thread Ratio: \t\t%.2f\n", issueR); 
            fprintf(status_logf, "===========================================\n\n");
            fflush(status_logf);
            fclose(status_logf);
        }
        pthread_mutex_unlock(&statusLogMutex);
        // --- End Status Log ---

        // --- GANTT: CAPTURE END TIME & LOG ---
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        pthread_mutex_lock(&ganttLogMutex); // Use GANTT mutex
        gantt_logf = fopen("gantt_log.csv", "a"); // Log to CSV
        if (gantt_logf) {
            fprintf(gantt_logf, "GANTT,Display_thread,%d,%ld,%ld,%ld,%ld\n", 
                    prio, start_time.tv_sec, start_time.tv_nsec, 
                    end_time.tv_sec, end_time.tv_nsec);
            fclose(gantt_logf);
        }
        pthread_mutex_unlock(&ganttLogMutex);
        // --- END GANTT ---

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL); 
    }
    return NULL;
}
// rtsounds.c

// **************** Lógica da Thread 3: Issue ****************
// rtsounds.c

// Variáveis globais (RTDB) necessárias para a Issue Thread (adicionar em rtsounds.h)
// volatile float detectedIssueFrequency = 0.0;
// volatile float issueRatio = 0.0; 
// volatile int issueDetected = 0;

// rtsounds.c

void* Issue_thread(void* arg) {
    const int N = ABUFSIZE_SAMPLES; 
    const int ISSUE_FREQ_THRESHOLD = 2000; 
    
    complex double x[N]; 
    float fk[N];
    float Ak[N];
    
    struct timespec period = {1, 0}; // 1.0 s
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("Issue Thread Running - Prio: %d\n", prio);

    while (1) {
        next_wakeup = TsAdd(next_wakeup, period);
        
        // --- GANTT: CAPTURE START TIME ---
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        // --- END GANTT ---

        buffer* readBuffer = cab_getReadBuffer(&cab_buffer); 

        if (readBuffer != NULL) {
            
            for (int k = 0; k < N; k++) {
                double centered_sample = (double)readBuffer->buf[k] - 32768.0; 
                x[k] = centered_sample + 0.0 * I;
            }
            
            cab_releaseReadBuffer(&cab_buffer, readBuffer->index); 

            fftCompute(x, N);
            fftGetAmplitude(x, N, SAMP_FREQ, fk, Ak);
            
            float maxHighFreqAmp = 0.0;
            float maxSpeedAmp = 0.0; 
            float currentIssueFreq = 0.0;

            for (int k = 1; k <= N/2; k++) {
                if (fk[k] >= ISSUE_FREQ_THRESHOLD && Ak[k] > maxHighFreqAmp) {
                    maxHighFreqAmp = Ak[k];
                    currentIssueFreq = fk[k];
                }
                if (fk[k] < ISSUE_FREQ_THRESHOLD && Ak[k] > maxSpeedAmp) {
                    maxSpeedAmp = Ak[k];
                }
            }
            
            float ratio = (maxSpeedAmp > 0) ? (maxHighFreqAmp / maxSpeedAmp) : 0.0;
            int issueFound = (ratio > 0.15 && maxHighFreqAmp > 8000.0); 

            pthread_mutex_lock(&updatedVarMutex);
            detectedIssueFrequency = currentIssueFreq;
            issueRatio = ratio;
            issueDetected = issueFound;
            pthread_mutex_unlock(&updatedVarMutex);
            
            printf("DEBUG ISSUE (Prio %d): High Amp=%.2f, Ratio=%.2f (Falha: %s)\n", prio, maxHighFreqAmp, ratio, issueFound ? "SIM" : "NÃO");
        } else {
            printf("DEBUG ISSUE (Prio %d): Sem buffer disponível, ignorando ciclo.\n", prio);
        }
        
        // --- GANTT: CAPTURE END TIME & LOG ---
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        pthread_mutex_lock(&ganttLogMutex); // Use GANTT mutex
        gantt_logf = fopen("gantt_log.csv", "a"); // Log to CSV
        if (gantt_logf) {
            fprintf(gantt_logf, "GANTT,Issue_thread,%d,%ld,%ld,%ld,%ld\n", 
                    prio, start_time.tv_sec, start_time.tv_nsec, 
                    end_time.tv_sec, end_time.tv_nsec);
            fclose(gantt_logf);
        }
        pthread_mutex_unlock(&ganttLogMutex);
        // --- END GANTT ---

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL); 
    }
    return NULL;
}

// **************** Lógica da Thread 4: Direction ****************
// rtsounds.c

void* Direction_thread(void* arg) {
    const int N = ABUFSIZE_SAMPLES; 
    
    struct timespec period = {0, 500000000}; 
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("Direction Thread Running - Prio: %d\n", prio);

    float prevSpeed = 0.0f;
    float prevAmp = 0.0f;
    
    const float MIN_SPEED_RUNNING = 50.0f;
    const float ACCEL_THRESHOLD = 20.0f;
    const float DECEL_THRESHOLD = 20.0f;
    const float STABLE_THRESHOLD = 10.0f;

    while (1) {
        next_wakeup = TsAdd(next_wakeup, period);

        // --- GANTT: CAPTURE START TIME ---
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        // --- END GANTT ---

        float currentSpeed = 0.0f;
        float currentAmp = 0.0f;

        pthread_mutex_lock(&updatedVarMutex);
        currentSpeed = detectedSpeedFrequency;
        currentAmp = maxAmplitudeDetected;
        pthread_mutex_unlock(&updatedVarMutex);

        float speedDelta = currentSpeed - prevSpeed;
        
        int newDirection = 0;  
        
        if (currentSpeed < MIN_SPEED_RUNNING) {
            newDirection = 0;
        } 
        else if (speedDelta > ACCEL_THRESHOLD) {
            newDirection = 1;
        } 
        else if (speedDelta < -DECEL_THRESHOLD) {
            newDirection = -1;
        } 
        else if (fabs(speedDelta) <= STABLE_THRESHOLD && currentSpeed >= MIN_SPEED_RUNNING) {
            newDirection = 2;
        }

        pthread_mutex_lock(&updatedVarMutex);
        directionValue = newDirection;
        directionValues.lastFrequency = currentSpeed;
        directionValues.lastAmplitude = currentAmp;
        pthread_mutex_unlock(&updatedVarMutex);

        prevSpeed = currentSpeed;
        prevAmp = currentAmp;

        const char *dirStr = (newDirection == 1) ? "ACCEL" : 
                            (newDirection == -1) ? "DECEL" : 
                            (currentSpeed < MIN_SPEED_RUNNING) ? "STOP" : "STABLE";
        
        printf("DEBUG DIRECTION (Prio %d): Speed=%.1f Hz (Δ=%.1f), State=%s\n",
               prio, currentSpeed, speedDelta, dirStr);

        // --- GANTT: CAPTURE END TIME & LOG ---
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        pthread_mutex_lock(&ganttLogMutex); // Use GANTT mutex
        gantt_logf = fopen("gantt_log.csv", "a"); // Log to CSV
        if (gantt_logf) {
            fprintf(gantt_logf, "GANTT,Direction_thread,%d,%ld,%ld,%ld,%ld\n", 
                    prio, start_time.tv_sec, start_time.tv_nsec, 
                    end_time.tv_sec, end_time.tv_nsec);
            fclose(gantt_logf);
        }
        pthread_mutex_unlock(&ganttLogMutex);
        // --- END GANTT ---

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL);
    }
    return NULL;
}
// **************** Lógica da Thread 6: FFT (ou 6ª Thread) ****************
void* FFT_thread(void* arg) {
    const int N = ABUFSIZE_SAMPLES;
    complex double x[N];
    float fk[N];
    float Ak[N];
    
    struct timespec period = {2, 0}; 
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup);

    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("FFT Spectral Analysis Thread Running - Prio: %d\n", prio);

    while (1) {
        next_wakeup = TsAdd(next_wakeup, period);
        
        // --- GANTT: CAPTURE START TIME ---
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        // --- END GANTT ---

        buffer* readBuffer = cab_getReadBuffer(&cab_buffer);
        
        if (readBuffer != NULL) {
            printf("DEBUG FFT: Processing buffer %d for spectral analysis\n", readBuffer->index);

            for (int k = 0; k < N; k++) {
                double centered_sample = (double)readBuffer->buf[k] - 32768.0;
                x[k] = centered_sample + 0.0 * I;
            }
            cab_releaseReadBuffer(&cab_buffer, readBuffer->index);

            fftCompute(x, N);
            fftGetAmplitude(x, N, SAMP_FREQ, fk, Ak);

            // --- Print to Console AND Status Log ---
            pthread_mutex_lock(&statusLogMutex);
            status_logf = fopen("rtsounds_log.txt", "a");

            printf("\n╔═══════════════════════════════════════════╗\n");
            printf("║   SPECTRAL ANALYSIS (Top 5 Peaks)        ║\n");
            printf("╠═══════════════════════════════════════════╣\n");
            if (status_logf) {
                fprintf(status_logf, "\n╔═══════════════════════════════════════════╗\n");
                fprintf(status_logf, "║   SPECTRAL ANALYSIS (Top 5 Peaks)        ║\n");
                fprintf(status_logf, "╠═══════════════════════════════════════════╣\n");
            }

            float Ak_copy[N/2 + 1];
            for (int k = 0; k <= N/2; k++) Ak_copy[k] = Ak[k];

            int peaks_found = 0;
            for (int p = 0; p < 5; p++) {
                float maxA = 0.0;
                int maxIdx = 0;
                for (int k = 1; k <= N/2; k++) {
                    if (Ak_copy[k] > maxA) {
                        maxA = Ak_copy[k];
                        maxIdx = k;
                    }
                }
                if (maxA > 100.0) {
                    printf("║ Peak %d: %7.1f Hz  │  Amp: %10.1f    ║\n", p + 1, fk[maxIdx], maxA);
                    if (status_logf) fprintf(status_logf, "║ Peak %d: %7.1f Hz  │  Amp: %10.1f    ║\n", p + 1, fk[maxIdx], maxA);
                    peaks_found++;
                }
                Ak_copy[maxIdx] = 0.0;
            }
            if (peaks_found == 0) {
                printf("║ No significant peaks detected (all < 100) ║\n");
                if (status_logf) fprintf(status_logf, "║ No significant peaks detected (all < 100) ║\n");
            }
            
            printf("╚═══════════════════════════════════════════╝\n\n");
            if (status_logf) {
                fprintf(status_logf, "╚═══════════════════════════════════════════╝\n\n");
                fflush(status_logf);
                fclose(status_logf);
            }
            pthread_mutex_unlock(&statusLogMutex);
            // --- End Status Log ---

        } else {
            printf("DEBUG FFT: No buffer available for spectral analysis\n");
        }

        // --- GANTT: CAPTURE END TIME & LOG ---
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        pthread_mutex_lock(&ganttLogMutex); // Use GANTT mutex
        gantt_logf = fopen("gantt_log.csv", "a"); // Log to CSV
        if (gantt_logf) {
            fprintf(gantt_logf, "GANTT,FFT_thread,%d,%ld,%ld,%ld,%ld\n", 
                    prio, start_time.tv_sec, start_time.tv_nsec, 
                    end_time.tv_sec, end_time.tv_nsec);
            fclose(gantt_logf);
        }
        pthread_mutex_unlock(&ganttLogMutex);
        // --- END GANTT ---

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL);
    }
    return NULL;
}

// Add Preprocessing thread function
void* Preprocessing_thread(void* arg) {
    struct timespec period = {0, 150000000}; // 150 ms
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup);

    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("Preprocessing Thread (Disabled) Running - Prio: %d\n", prio);

    while (1) {
        next_wakeup = TsAdd(next_wakeup, period);

        // --- GANTT: CAPTURE START TIME ---
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        // --- END GANTT ---

        // This thread is disabled.
        // We log its start and end time around the sleep.

        // --- GANTT: CAPTURE END TIME & LOG ---
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        pthread_mutex_lock(&ganttLogMutex); // Use GANTT mutex
        gantt_logf = fopen("gantt_log.csv", "a"); // Log to CSV
        if (gantt_logf) {
            fprintf(gantt_logf, "GANTT,Preproc_thread,%d,%ld,%ld,%ld,%ld\n", 
                    prio, start_time.tv_sec, start_time.tv_nsec, 
                    end_time.tv_sec, end_time.tv_nsec);
            fclose(gantt_logf);
        }
        pthread_mutex_unlock(&ganttLogMutex);
        // --- END GANTT ---

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL);
    }
    return NULL;
}

/* *************************
* SDL Initialization Function
* *************************/
int initialize_sdl_audio(int index) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_AudioSpec desiredRecordingSpec = {
        .freq = SAMP_FREQ, .format = FORMAT, .channels = MONO, .samples = ABUFSIZE_SAMPLES, .callback = audioRecordingCallback
    };

    int device_count = SDL_GetNumAudioDevices(SDL_TRUE);
    if (device_count < 1) {
        printf("Unable to get audio capture device! SDL Error: %s\n", SDL_GetError());
        return 1;
    }

    for (int i = 0; i < device_count; ++i) printf("%d - %s\n", i, SDL_GetAudioDeviceName(i, SDL_TRUE));

    if (index < 0 || index >= device_count) {
        printf("Invalid device ID. Must be between 0 and %d\n", device_count - 1);
        return 1;
    }

    recordingDeviceId = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(index, SDL_TRUE), SDL_TRUE, &desiredRecordingSpec, &gReceivedRecordingSpec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if (recordingDeviceId == 0) {
        printf("Failed to open recording device! SDL Error: %s\n", SDL_GetError());
        return 1;
    }

    // Compute and store bytes per sample/frame for use in the callback
    gBytesPerSample = gReceivedRecordingSpec.channels * (SDL_AUDIO_BITSIZE(gReceivedRecordingSpec.format) / 8);
    int bytesPerSecond = gReceivedRecordingSpec.freq * gBytesPerSample;
    gBufferByteSize = RECORDING_BUFFER_SECONDS * bytesPerSecond;
    gBufferByteMaxPosition = MAX_RECORDING_SECONDS * bytesPerSecond;

    // Print actual runtime audio spec for debugging
    fprintf(stderr, "Recording device opened: freq=%d channels=%d format=0x%x samples=%d bytesPerSample=%d\n",
            gReceivedRecordingSpec.freq, gReceivedRecordingSpec.channels, (int)gReceivedRecordingSpec.format, gReceivedRecordingSpec.samples, gBytesPerSample);

    return 0;
}
pthread_t thread1, thread2, thread3, thread4, thread5, thread6, thread7;
struct sched_param parm1, parm2, parm3, parm4, parm5, parm6, parm7;
pthread_attr_t attr1, attr2, attr3, attr4, attr5, attr6, attr7;

int priorities[7];

void usage() {
    printf("Usage: ./rtsounds -prio [p1 p2 p3 p4 p5 p6 p7]\n");
}

void cleanup() {
    SDL_CloseAudioDevice(recordingDeviceId);
    SDL_Quit();
}

void handle_signal(int signal) {
    cleanup();
    exit(0);
}
// Audio callback is defined in the header file
/* *************************
* Main Function
* *************************/
int main(int argc, char *argv[]) {
    // General vars
    int err;
    struct sched_param parm1 = {0}, parm2 = {0}, parm3 = {0}, 
                       parm4 = {0}, parm5 = {0}, parm6 = {0}, parm7 = {0};
    
    unsigned char* procname1 = "AudioThread";
    unsigned char* procname2 = "PreprocessingThread";
    unsigned char* procname3 = "SpeedThread";
    unsigned char* procname4 = "IssueThread";
    unsigned char* procname5 = "DirectionThread";
    unsigned char* procname6 = "DisplayThread";
    unsigned char* procname7 = "FFTThread";
    
    // Parse priorities from command line
    if (argc > 1) {
        if (strcmp(argv[1], "-prio") == 0 && argc == 9) {
            for(int i = 0; i < 7; i++) {
                priorities[i] = atoi(argv[i+2]);
            }
        } else {
            usage();
            return 1;
        }
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return 1;
    }
    atexit(cleanup);

    // List and select audio devices
    int gRecordingDeviceCount = SDL_GetNumAudioDevices(SDL_TRUE);
    if (gRecordingDeviceCount < 1) {
        fprintf(stderr, "No recording devices found!\n");
        return 1;
    }

    printf("Available recording devices:\n");
    for(int i = 0; i < gRecordingDeviceCount; ++i) {
        printf("%d - %s\n", i, SDL_GetAudioDeviceName(i, SDL_TRUE));
    }

    int index;
    printf("Choose audio device: ");
    scanf("%d", &index);
    
    if (index < 0 || index >= gRecordingDeviceCount) {
        fprintf(stderr, "Invalid device index!\n");
        return 1;
    }

    // Initialize audio with selected device
    if (initialize_sdl_audio(index) != 0) {
        return 1;
    }

    // Allocate recording buffer
    gRecordingBuffer = (uint8_t *)malloc(gBufferByteSize);
    memset(gRecordingBuffer, 0, gBufferByteSize);

    // Initialize CAB buffer
    init_cab(&cab_buffer);
    printf("CAB Buffer Initialization:\n");
    for (int i = 0; i < NTASKS + 1; i++) {
        printf("Buffer %d: nusers=%d\n", i, cab_buffer.buflist[i].nusers);
    }

    // semaphore initialization 
    sem_init(&data_ready, 0, 0);

    // Clear Gantt Log and write CSV header
    gantt_logf = fopen("gantt_log.csv", "w");
    if (gantt_logf) {
        fprintf(gantt_logf, "# TaskName,Priority,StartSec,StartNsec,EndSec,EndNsec\n");
        fclose(gantt_logf);
    } else {
        perror("Failed to open gantt_log.csv for writing");
    }

    // Clear Status Log
    status_logf = fopen("rtsounds_log.txt", "w");
    if (status_logf) {
        fprintf(status_logf, "RTSounds Log Initialized.\n\n");
        fclose(status_logf);
    } else {
        perror("Failed to open rtsounds_log.txt for writing");
    }

    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Create threads with their priorities
    // Thread 1: Audio
    pthread_attr_init(&attr1);
    pthread_attr_setinheritsched(&attr1, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr1, SCHED_FIFO);
    parm1.sched_priority = priorities[0];
    pthread_attr_setschedparam(&attr1, &parm1);
    err = pthread_create(&thread1, &attr1, Audio_thread, &parm1);
    if (err != 0) {
        fprintf(stderr, "Error creating Audio thread\n");
        return 1;
    }

    // Thread 2: Preprocessing (LP Filter)
    pthread_attr_init(&attr2);
    pthread_attr_setinheritsched(&attr2, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr2, SCHED_FIFO);
    parm2.sched_priority = priorities[1];
    pthread_attr_setschedparam(&attr2, &parm2);
    err = pthread_create(&thread2, &attr2, Preprocessing_thread, &parm2);
    if (err != 0) {
        fprintf(stderr, "Error creating Preprocessing thread\n");
        return 1;
    }

    // Thread 3: Speed
    pthread_attr_init(&attr3);
    pthread_attr_setinheritsched(&attr3, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr3, SCHED_FIFO);
    parm3.sched_priority = priorities[2];
    pthread_attr_setschedparam(&attr3, &parm3);
    err = pthread_create(&thread3, &attr3, Speed_thread, &parm3); 
    if (err != 0) {
        printf("\n\r Error creating Thread [%s]", strerror(err));
        return 1;
    }

    // Thread 4: Issue
    pthread_attr_init(&attr4);
    pthread_attr_setinheritsched(&attr4, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr4, SCHED_FIFO);
    parm4.sched_priority = priorities[3];
    pthread_attr_setschedparam(&attr4, &parm4);
    err = pthread_create(&thread4, &attr4, Issue_thread, &parm4); 
    if (err != 0) {
        printf("\n\r Error creating Thread 4 (Issue) [%s]", strerror(err));
        return 1;
    }

    // Thread 5: Direction
    pthread_attr_init(&attr5);
    pthread_attr_setinheritsched(&attr5, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr5, SCHED_FIFO);
    parm5.sched_priority = priorities[4];
    pthread_attr_setschedparam(&attr5, &parm5);
    err = pthread_create(&thread5, &attr5, Direction_thread, &parm5); 
    if (err != 0) {
        printf("\n\r Error creating Thread 5 (Direction) [%s]", strerror(err));
        return 1;
    }

    // Thread 6: Display
    pthread_attr_init(&attr6);
    pthread_attr_setinheritsched(&attr6, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr6, SCHED_FIFO);
    parm6.sched_priority = priorities[5];
    pthread_attr_setschedparam(&attr6, &parm6);
    err = pthread_create(&thread6, &attr6, Display_thread, &parm6);
    if (err != 0) {
        printf("\n\r Error creating Thread 6 (Display) [%s]", strerror(err));
        return 1;
    }

    // Thread 7: FFT
    pthread_attr_init(&attr7);
    pthread_attr_setinheritsched(&attr7, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr7, SCHED_FIFO);
    parm7.sched_priority = priorities[6];
    pthread_attr_setschedparam(&attr7, &parm7);
    err = pthread_create(&thread7, &attr7, FFT_thread, &parm7);
    if (err != 0) {
        printf("\n\r Error creating Thread 7 (FFT) [%s]", strerror(err));
        return 1;
    }

    while(1); // Main loop

    return 0;
}

/* ***********************************************
* Auxiliary Functions
* ************************************************/
buffer* cab_getWriteBuffer(cab* c) {
    for (int i = 0; i < NTASKS + 1; i++) {
        if (c->buflist[i].nusers == 0) {
            pthread_mutex_lock(&c->buflist[i].bufMutex);
            return &c->buflist[i];
        }
    }
    return NULL;  // Return NULL if no available buffer is found
}

buffer* cab_getReadBuffer(cab* c) {
    while (pthread_mutex_trylock(&c->buflist[c->last_write].bufMutex) != 0) {
        SDL_Delay(10);  // Add a small delay to reduce CPU usage
    }
    c->buflist[c->last_write].nusers += 1;
    pthread_mutex_unlock(&c->buflist[c->last_write].bufMutex);
    return &c->buflist[c->last_write];
}

void cab_releaseWriteBuffer(cab* c, uint8_t index) {
    pthread_mutex_unlock(&c->buflist[index].bufMutex);
    c->last_write = index;
}

void cab_releaseReadBuffer(cab* c, uint8_t index) {
    c->buflist[index].nusers--;
    pthread_mutex_unlock(&c->buflist[index].bufMutex);
}

void init_cab(cab *cab_obj) {
    cab_obj->last_write = 0;
    for (int i = 0; i < NTASKS + 1; i++) {
        memset(cab_obj->buflist[i].buf, 0, sizeof(cab_obj->buflist[i].buf));
        cab_obj->buflist[i].nusers = 0;
        cab_obj->buflist[i].index = i;
        pthread_mutex_init(&cab_obj->buflist[i].bufMutex, NULL);
    }
}

void audioRecordingCallback(void* userdata, Uint8* stream, int len) {
    // --- GANTT: CAPTURE START TIME ---
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    // --- END GANTT ---

    buffer* writeBuffer = cab_getWriteBuffer(&cab_buffer);
    
    if (writeBuffer != NULL && len == BUF_SIZE * sizeof(uint16_t)) {
        memcpy(writeBuffer->buf, stream, len);
        cab_releaseWriteBuffer(&cab_buffer, writeBuffer->index);
        sem_post(&data_ready);
    }
    
    // --- GANTT: CAPTURE END TIME & LOG ---
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    pthread_mutex_lock(&ganttLogMutex); // Use GANTT mutex
    gantt_logf = fopen("gantt_log.csv", "a"); // Log to CSV
    if (gantt_logf) {
        fprintf(gantt_logf, "GANTT,AudioCallback,%d,%ld,%ld,%ld,%ld\n", 
                99, start_time.tv_sec, start_time.tv_nsec, 
                end_time.tv_sec, end_time.tv_nsec);
        fclose(gantt_logf);
    }
    pthread_mutex_unlock(&ganttLogMutex);
    // --- END GANTT ---
}


void filterLP(uint32_t cof, uint32_t sampleFreq, uint8_t *buffer, uint32_t nSamples) {
    uint16_t *procBuffer = malloc(nSamples * sizeof(uint16_t));
    uint16_t *origBuffer = (uint16_t *)buffer;
    float alfa = (2 * M_PI / sampleFreq * cof) / ((2 * M_PI / sampleFreq * cof) + 1);
    float beta = 1 - alfa;

    procBuffer[0] = origBuffer[0];
    for (int i = 1; i < nSamples; i++) {
        procBuffer[i] = alfa * origBuffer[i] + beta * procBuffer[i - 1];
    }

    memcpy(buffer, (uint8_t *)procBuffer, nSamples * sizeof(uint16_t));
    free(procBuffer);
}

void save_audio_to_wav(const char* filename, Uint8* buffer, Uint32 buffer_size, int sample_rate) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("Error opening file for writing\n");
        return;
    }

    // Write the WAV header
    fwrite("RIFF", 1, 4, file);
    int32_t chunk_size = buffer_size + 36;
    fwrite(&chunk_size, 4, 1, file);
    fwrite("WAVE", 1, 4, file);
    fwrite("fmt ", 1, 4, file);

    int32_t subchunk1_size = 16;
    fwrite(&subchunk1_size, 4, 1, file);

    int16_t audio_format = 1; // PCM
    fwrite(&audio_format, 2, 1, file);
    int16_t num_channels = 1; // Mono
    fwrite(&num_channels, 2, 1, file);
    fwrite(&sample_rate, 4, 1, file);
    int32_t byte_rate = sample_rate * num_channels * sizeof(uint16_t);
    fwrite(&byte_rate, 4, 1, file);
    int16_t block_align = num_channels * sizeof(uint16_t);
    fwrite(&block_align, 2, 1, file);
    int16_t bits_per_sample = 16;
    fwrite(&bits_per_sample, 2, 1, file);

    fwrite("data", 1, 4, file);
    fwrite(&buffer_size, 4, 1, file);

    // Write the audio data
    fwrite(buffer, 1, buffer_size, file);

    fclose(file);
}

/* ***********************************************
 * Debug function: Prints the buffer contents - 8 bit samples
 * ***********************************************/
void printSamplesU8(uint8_t *buffer, int size) {
    printf("\nSamples (8-bit): \n");
    for (int i = 0; i < size; i++) {
        printf("%3u ", buffer[i]);
        if ((i + 1) % 20 == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

/* ***********************************************
 * Debug function: Prints the buffer contents - 16 bit samples
 * ***********************************************/
void printSamplesU16(uint8_t *buffer, int nsamples) {
    printf("\nSamples (16-bit): \n");
    uint16_t *bufu16 = (uint16_t *)buffer;
    for (int i = 0; i < nsamples; i++) {
        printf("%5u ", bufu16[i]);
        if ((i + 1) % 20 == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

// Add this helper at file scope (not inside any other function)
void dumpCapturedBufferToWav(const char *filename) {
    if (gReceivedRecordingSpec.freq <= 0) {
        fprintf(stderr, "dumpCapturedBufferToWav: invalid sample rate\n");
        return;
    }

    // warn if format/channels differ (still attempt dump)
    if (gReceivedRecordingSpec.format != FORMAT || gReceivedRecordingSpec.channels != MONO) {
        fprintf(stderr, "dumpCapturedBufferToWav: unexpected format=0x%x channels=%d\n",
                (int)gReceivedRecordingSpec.format, gReceivedRecordingSpec.channels);
    }

    buffer* readBuffer = cab_getReadBuffer(&cab_buffer);
    if (!readBuffer) {
        fprintf(stderr, "dumpCapturedBufferToWav: no buffer available\n");
        return;
    }

    size_t bytes = ABUFSIZE_SAMPLES * sizeof(uint16_t);
    save_audio_to_wav(filename, (Uint8*)readBuffer->buf, (Uint32)bytes, gReceivedRecordingSpec.freq);

    cab_releaseReadBuffer(&cab_buffer, readBuffer->index);
    fprintf(stderr, "WAV dump written: %s (fs=%d, bytes=%zu)\n", filename, gReceivedRecordingSpec.freq, bytes);
}
