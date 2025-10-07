#define _GNU_SOURCE /* Must precede #include <sched.h> for sched_setaffinity */ 
#define __USE_MISC
#define ABUFSIZE_SAMPLES 4096
#define NTASKS 6
#include "rtsounds.h"
#include <math.h>
#include <complex.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SDL.h>
#include <semaphore.h>

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

volatile float detectedSpeedFrequency = 0.0, maxAmplitudeDetected = 0.0;

pthread_mutex_t updatedVarMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t updatedVar = PTHREAD_COND_INITIALIZER;
sem_t data_ready;  // Semaphore for managing data readines
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
    // N é definido a partir da macro ABUFSIZE_SAMPLES
    const int N = ABUFSIZE_SAMPLES; 
    
    // ********************************************
    // CORREÇÃO: Declaração dos arrays necessários para FFT
    // (Isto resolve os erros 'x', 'fk' e 'Ak' undeclared)
    // ********************************************
    complex double x[N]; 
    float fk[N];
    float Ak[N];
    // ********************************************
    
    // NOTA: A linha 'const int SAMP_FREQ = 44100;' FOI REMOVIDA 
    // (Isto resolve o erro 'expected identifier or ( before numeric constant')

    // 1. Definir o período (e.g., 200 ms)
    struct timespec period = {0, 200000000}; // 200ms
    struct timespec next_wakeup;

    // Inicializar o tempo do próximo despertar (RT)
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority; 
    printf("Speed Thread Running - Prio: %d\n", prio);

    while (1) {
        // 2. Definir o próximo tempo de despertar (antes de esperar pelos dados)
        next_wakeup = TsAdd(next_wakeup, period);
        
        printf("DEBUG SPEED: A Esperar por dados (sem_wait)...\n"); 
        sem_wait(&data_ready);
        
        printf("DEBUG SPEED: Dados recebidos! A processar...\n"); 
        
        buffer* readBuffer = cab_getReadBuffer(&cab_buffer); 

        if (readBuffer != NULL) {
            
            // 5. Centrar as amostras (AUDIO_U16) e converter para complex double
            for (int k = 0; k < N; k++) {
                double centered_sample = (double)readBuffer->buf[k] - 32768.0; 
                x[k] = centered_sample + 0.0 * I;
            }
            
            // 6. Libertar o buffer 
            cab_releaseReadBuffer(&cab_buffer, readBuffer->index); 

            // 7. Executar FFT
            fftCompute(x, N);
            
            // 8. Obter Amplitudes (usando a macro SAMP_FREQ)
            fftGetAmplitude(x, N, SAMP_FREQ, fk, Ak); // Chamar a função
            
            // 9. Lógica de deteção de velocidade
            float maxA = 0.0;
            float maxF = 0.0;
            for(int k=1; k<=N/2; k++) {
                if (Ak[k] > maxA) {
                    maxA = Ak[k];
                    maxF = fk[k];
                }
            }
            
            // 10. Atualizar RTDB
            pthread_mutex_lock(&updatedVarMutex);
            detectedSpeedFrequency = maxF;
            maxAmplitudeDetected = maxA;
            pthread_mutex_unlock(&updatedVarMutex);
            
            printf("DEBUG SPEED: Max Freq=%.2f Hz, Max Amp=%.2f (Loop concluído)\n", maxF, maxA);
        }
        
        // 11. Periodic Wait (Tempo Real Absoluto)
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL); 
    }
    return NULL;
}
// Adicionar ao lado das outras thread functions
// rtsounds.c

void* Display_thread(void* arg) {
    // ******************************************************
    // CORREÇÃO: Variáveis locais do ciclo de Tempo Real (RT)
    // ******************************************************
    struct timespec period = {0, 250000000}; // 250 ms
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority;
    // ******************************************************

    printf("Display Thread Running - Prio: %d\n", prio);

    while (1) {
        // 2. Calcular o próximo tempo de despertar
        next_wakeup = TsAdd(next_wakeup, period);
        
        // Variáveis locais para armazenar o estado lido do RTDB
        float speedFreq = 0.0;
        float maxAmp = 0.0;
        int isIssue = 0;
        int direction = 0; 
        float issueR = 0.0; // Variável local para issueRatio

        // 3. Aceder ao RTDB com Mutex para leitura segura
        pthread_mutex_lock(&updatedVarMutex);
        
        // Leitura das variáveis (agora que estão no rtsounds.h, não causam erro)
        speedFreq = detectedSpeedFrequency;
        maxAmp = maxAmplitudeDetected;
        isIssue = issueDetected; 
        direction = directionValue; 
        issueR = issueRatio; // Lendo a variável global
        
        pthread_mutex_unlock(&updatedVarMutex);

        // 4. Apresentar Resultados 
        system("clear"); 

        // Conversão de valor numérico para string (apenas para display)
        const char *issueStatus = isIssue ? "FAULT DETECTED! (High Prio 60)" : "OK";
        const char *dirStatus = (direction == 1) ? "FORWARD" : (direction == -1) ? "REVERSE" : "STOP";
        
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
        printf("===========================================\n");

        // 5. Espera Periódica (RT)
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
    const int ISSUE_FREQ_THRESHOLD = 2000; // Analisar picos acima de 2 kHz
    
    // Variáveis locais para FFT (necessárias para evitar erros de compilação)
    complex double x[N]; 
    float fk[N];
    float Ak[N];
    
    // 1. Definir o período (1 segundo)
    struct timespec period = {1, 0}; // 1.0 s
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("Issue Thread Running - Prio: %d\n", prio);

    while (1) {
        // 2. Definir o próximo tempo de despertar (periodicidade RT)
        next_wakeup = TsAdd(next_wakeup, period);
        
        // 3. Obter o buffer de leitura do CAB
        buffer* readBuffer = cab_getReadBuffer(&cab_buffer); 

        if (readBuffer != NULL) {
            
            // 4. Preparar dados para FFT (centrar amostras)
            for (int k = 0; k < N; k++) {
                double centered_sample = (double)readBuffer->buf[k] - 32768.0; 
                x[k] = centered_sample + 0.0 * I;
            }
            
            // 5. Libertar o buffer para escrita
            cab_releaseReadBuffer(&cab_buffer, readBuffer->index); 

            // 6. Executar FFT e obter Amplitudes
            fftCompute(x, N);
            fftGetAmplitude(x, N, SAMP_FREQ, fk, Ak);
            
            // 7. LÓGICA DE DETEÇÃO DE FALHAS
            float maxHighFreqAmp = 0.0;
            float maxSpeedAmp = 0.0; 
            float currentIssueFreq = 0.0;

            // Encontrar o pico de amplitude (Falha) em alta frequência
            for (int k = 1; k <= N/2; k++) {
                if (fk[k] >= ISSUE_FREQ_THRESHOLD && Ak[k] > maxHighFreqAmp) {
                    maxHighFreqAmp = Ak[k];
                    currentIssueFreq = fk[k];
                }
                // Encontrar o pico na zona de baixa frequência (Velocidade) para o rácio
                if (fk[k] < ISSUE_FREQ_THRESHOLD && Ak[k] > maxSpeedAmp) {
                    maxSpeedAmp = Ak[k];
                }
            }
            
            // 8. Calcular o Rácio e Determinar a Falha
            float ratio = (maxSpeedAmp > 0) ? (maxHighFreqAmp / maxSpeedAmp) : 0.0;
            // Critério de Falha: Pico de alta frequência ser significativo (> 10k) E
            // ter um rácio considerável (> 20%) em relação ao pico de velocidade.
            int issueFound = (ratio > 0.20 && maxHighFreqAmp > 10000.0); 

            // 9. Atualizar o RTDB (acesso exclusivo com Mutex)
            pthread_mutex_lock(&updatedVarMutex);
            detectedIssueFrequency = currentIssueFreq;
            issueRatio = ratio;
            issueDetected = issueFound;
            pthread_mutex_unlock(&updatedVarMutex);
            
            printf("DEBUG ISSUE (Prio %d): High Amp=%.2f, Ratio=%.2f (Falha: %s)\n", prio, maxHighFreqAmp, ratio, issueFound ? "SIM" : "NÃO");
        } else {
            printf("DEBUG ISSUE (Prio %d): Sem buffer disponível, ignorando ciclo.\n", prio);
        }
        
        // 10. Espera Periódica (Tempo Real Absoluto)
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL); 
    }
    return NULL;
}

// **************** Lógica da Thread 4: Direction ****************
// rtsounds.c

void* Direction_thread(void* arg) {
    const int N = ABUFSIZE_SAMPLES; 
    
    // Variáveis locais para FFT (declaradas, mas não utilizadas nesta lógica simplificada de RTDB)
    // São mantidas aqui apenas para consistência, se decidir implementar a FFT nesta thread mais tarde.
    complex double x[N]; 
    float fk[N];
    float Ak[N];
    
    // 1. Definir o período (500 ms)
    struct timespec period = {0, 500000000}; 
    struct timespec next_wakeup;
    clock_gettime(CLOCK_MONOTONIC, &next_wakeup); 

    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("Direction Thread Running - Prio: %d\n", prio);

    while (1) {
        // 2. Definir o próximo tempo de despertar (periodicidade RT)
        next_wakeup = TsAdd(next_wakeup, period);
        
        // 3. Obter o buffer de leitura do CAB (Não usado, pois lê do RTDB)
        
        // 4. LER RTDB (Pico de Velocidade do ciclo anterior)
        float currentSpeedFreq = 0.0;
        float currentMaxAmp = 0.0;
        float lastFreq = 0.0;
        float lastAmp = 0.0;
        
        // CORREÇÃO APLICADA AQUI: pthread_mutex_unlock
        pthread_mutex_lock(&updatedVarMutex);
        currentSpeedFreq = detectedSpeedFrequency;
        currentMaxAmp = maxAmplitudeDetected;
        lastFreq = directionValues.lastFrequency;
        lastAmp = directionValues.lastAmplitude;
        pthread_mutex_unlock(&updatedVarMutex); // <-- CORRETO

        
        // 5. LÓGICA DE DETEÇÃO DE DIREÇÃO
        int newDirection = directionValue; // Começa com o valor global anterior

        if (currentSpeedFreq > 0.5) { // Se o motor estiver a rodar (> 0.5 Hz)
            if (currentSpeedFreq > lastFreq && currentMaxAmp > lastAmp) {
                // Aceleração = FORWARD
                newDirection = 1;
            } else if (currentSpeedFreq < lastFreq && currentMaxAmp < lastAmp) {
                // Desaceleração = REVERSE
                newDirection = -1; 
            } else if (abs(currentSpeedFreq - lastFreq) < 0.5 && abs(currentMaxAmp - lastAmp) < 500) {
                 // Estável (mantém a direção anterior, a menos que esteja parado)
                newDirection = directionValue; 
            }
        } else {
            // Motor Parado
            newDirection = 0;
        }

        // 6. Atualizar o RTDB
        // CORREÇÃO APLICADA AQUI: pthread_mutex_unlock
        pthread_mutex_lock(&updatedVarMutex);
        directionValue = newDirection; // Atualiza a variável display
        directionValues.lastFrequency = currentSpeedFreq; // Guarda o estado atual
        directionValues.lastAmplitude = currentMaxAmp;
        pthread_mutex_unlock(&updatedVarMutex); // <-- CORRETO
        
        printf("DEBUG DIRECTION (Prio %d): Curr Freq=%.2f, Last Freq=%.2f, Dir=%d\n", prio, currentSpeedFreq, lastFreq, newDirection);
        
        // 7. Espera Periódica (Tempo Real Absoluto)
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wakeup, NULL); 
    }
    return NULL;
}
// **************** Lógica da Thread 6: FFT (ou 6ª Thread) ****************
void* FFT_thread(void* arg) {
    int prio = ((struct sched_param*)arg)->sched_priority;
    printf("FFT Thread Running - Prio: %d\n", prio);
    // TODO: Implementar periodicidade
    while(1) { SDL_Delay(250); } // Placeholder
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

    int bytesPerSample = gReceivedRecordingSpec.channels * (SDL_AUDIO_BITSIZE(gReceivedRecordingSpec.format) / 8);
    int bytesPerSecond = gReceivedRecordingSpec.freq * bytesPerSample;
    gBufferByteSize = RECORDING_BUFFER_SECONDS * bytesPerSecond;
    gBufferByteMaxPosition = MAX_RECORDING_SECONDS * bytesPerSecond;

    return 0;
}
pthread_t thread1, thread2, thread3, thread4, thread5, thread6;
struct sched_param parm1, parm2, parm3, parm4, parm5, parm6;
pthread_attr_t attr1, attr2, attr3, attr4, attr5, attr6;

int priorities[6];

void usage() {
    printf("Usage: ./rtsounds -prio [p1 p2 p3 p4 p5 p6]\n");
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
                       parm4 = {0}, parm5 = {0}, parm6 = {0};
    
    unsigned char* procname1 = "AudioThread";
    unsigned char* procname2 = "SpeedThread";
    unsigned char* procname3 = "IssueThread";
    unsigned char* procname4 = "DirectionThread";
    unsigned char* procname5 = "DisplayThread";
    unsigned char* procname6 = "FFTThread";
    
    // Parse priorities from command line
    if (argc > 1) {
        if (strcmp(argv[1], "-prio") == 0 && argc == 8) {
            for(int i = 0; i < 6; i++) {
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
// Thread 2: Speed
    pthread_attr_init(&attr2);
    pthread_attr_setinheritsched(&attr2, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr2, SCHED_FIFO);
    parm2.sched_priority = priorities[1];
    pthread_attr_setschedparam(&attr2, &parm2);
    // CORREÇÃO: Mudar &procname2 para &parm2
    err = pthread_create(&thread2, &attr2, Speed_thread, &parm2); 
    if (err != 0) {
        printf("\n\r Error creating Thread [%s]", strerror(err));
        return 1;
    }

    // Criação da thread3 (Issue_thread)
    // Criação da thread3 (Issue_thread - priorities[2])
    pthread_attr_init(&attr3);
    pthread_attr_setinheritsched(&attr3, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr3, SCHED_FIFO);
    parm3.sched_priority = priorities[2]; // Índice 2 (Prio 60, mais alta)
    pthread_attr_setschedparam(&attr3, &parm3);
    err = pthread_create(&thread3, &attr3, Issue_thread, &parm3); 
    if (err != 0) {
        printf("\n\r Error creating Thread 3 (Issue) [%s]", strerror(err));
        return 1;
    }

    // Criação da thread4 (Direction_thread - priorities[3])
    pthread_attr_init(&attr4);
    pthread_attr_setinheritsched(&attr4, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr4, SCHED_FIFO);
    parm4.sched_priority = priorities[3]; // Índice 3 (Prio 50)
    pthread_attr_setschedparam(&attr4, &parm4);
    err = pthread_create(&thread4, &attr4, Direction_thread, &parm4); 
    if (err != 0) {
        printf("\n\r Error creating Thread 4 (Direction) [%s]", strerror(err));
        return 1;
    }

    // ... Antes da criação da thread5 (Display_thread) ...
    // Continue similarly for thread3, thread4, thread5, thread6
    pthread_attr_init(&attr5);
    pthread_attr_setinheritsched(&attr5, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr5, SCHED_FIFO);
    parm5.sched_priority = priorities[4]; // Índice 4 (Prio 30)
    pthread_attr_setschedparam(&attr5, &parm5);
    err = pthread_create(&thread5, &attr5, Display_thread, &parm5); // Usar &parm5 para passar a prioridade
    if (err != 0) {
        printf("\n\r Error creating Thread 5 [%s]", strerror(err));
        return 1;
    }

    // Criação da thread6 (FFT_thread - priorities[5])
    pthread_attr_init(&attr6);
    pthread_attr_setinheritsched(&attr6, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr6, SCHED_FIFO);
    parm6.sched_priority = priorities[5]; // Índice 5 (Prio 20, mais baixa)
    pthread_attr_setschedparam(&attr6, &parm6);
    err = pthread_create(&thread6, &attr6, FFT_thread, &parm6); // Assumindo FFT_thread
    if (err != 0) {
        printf("\n\r Error creating Thread 6 (FFT) [%s]", strerror(err));
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
    // 1. Obter o buffer de escrita do cab_buffer global
    buffer* writeBuffer = cab_getWriteBuffer(&cab_buffer);
    
    // len deve ser igual a ABUFSIZE_SAMPLES * sizeof(uint16_t)
    if (writeBuffer != NULL && len == BUF_SIZE * sizeof(uint16_t)) {
        
        // 2. Copiar os dados (raw bytes do SDL) para o buffer do CAB (uint16_t)
        // BUF_SIZE (4096) é em samples (uint16_t), len (8192) é em bytes
        memcpy(writeBuffer->buf, stream, len);

        // 3. Libertar o buffer de escrita, sinalizando que está pronto para leitura
        cab_releaseWriteBuffer(&cab_buffer, writeBuffer->index);

        // 4. Sinalizar às threads consumidoras (como a Speed_thread) que há dados prontos
        sem_post(&data_ready);
    }
    
    // *OPCIONAL: Lógica original do gRecordingBuffer (gravação longa) deve ser removida ou desativada
    // Se o dispositivo estiver aberto, o callback vai continuar a ser chamado de forma contínua.
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
