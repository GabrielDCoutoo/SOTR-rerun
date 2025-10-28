#include <SDL.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MONO 1
#define SAMP_FREQ 44100
#define FORMAT AUDIO_U16
#define ABUFSIZE_SAMPLES 4096

typedef enum {
    TEST_CONSTANT_SPEED, TEST_ACCELERATION, TEST_DECELERATION,
    TEST_WITH_FAULT, TEST_START_STOP, TEST_MULTIPLE_HARMONICS
} TestScenario;

SDL_AudioDeviceID playbackDeviceId = 0;
SDL_AudioSpec gReceivedPlaybackSpec;
Uint8 *gPlaybackBuffer = NULL;
Uint32 gBufferBytePosition = 0;
Uint32 gBufferByteSize = 0;

/* --- Signal Generation --- */

void genSineU16(float freq, uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset) {
    if (!buffer || freq < 0 || durationMS == 0 || amp == 0) return;
    uint32_t nSamples = (uint32_t)(((float)durationMS / 1000.0f) * SAMP_FREQ);
    if (nSamples == 0) return;
    uint16_t *bufU16 = (uint16_t *)buffer;
    double sinArgInc = 2.0 * M_PI * freq / SAMP_FREQ;
    double currentPhase = 0.0;
    uint16_t halfAmp = amp / 2;
    for (uint32_t i = 0; i < nSamples; i++) {
        bufU16[offset + i] = 32768 + (int16_t)(halfAmp * sin(currentPhase));
        currentPhase += sinArgInc;
    }
}

// Placeholder/Removed: If you need to add signals, implement this properly.
// void addSineU16(float freq, uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset) {
//    printf("Warning: addSineU16 not fully implemented.\n");
// }

void genChirpU16(float startFreq, float endFreq, uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset) {
    if (!buffer || durationMS == 0 || amp == 0) return;
    uint32_t nSamples = (uint32_t)(((float)durationMS / 1000.0f) * SAMP_FREQ);
     if (nSamples == 0) return;
    uint16_t *bufU16 = (uint16_t *)buffer;
    double phase = 0.0;
    uint16_t halfAmp = amp / 2;
    for (uint32_t i = 0; i < nSamples; i++) {
        float currentFreq = startFreq + (endFreq - startFreq) * ((float)i / nSamples);
        phase += 2.0 * M_PI * currentFreq / SAMP_FREQ;
        bufU16[offset + i] = 32768 + (int16_t)(halfAmp * sin(phase));
    }
}

/* --- Audio Callback --- */
void audioPlaybackCallback(void *userdata, Uint8 *stream, int len) {
    if (gBufferBytePosition >= gBufferByteSize) gBufferBytePosition = 0; // Loop buffer
    Uint32 remainingBytes = gBufferByteSize - gBufferBytePosition;
    Uint32 bytesToCopy = (len > remainingBytes) ? remainingBytes : len;
    if (bytesToCopy > 0 && gPlaybackBuffer != NULL) {
        memcpy(stream, &gPlaybackBuffer[gBufferBytePosition], bytesToCopy);
    }
     if (bytesToCopy < len) { // Fill rest with silence
         uint16_t silence = 32768;
         uint16_t* stream16 = (uint16_t*)(stream + bytesToCopy);
         int samplesToSilence = (len - bytesToCopy) / sizeof(uint16_t);
         for(int i=0; i < samplesToSilence; ++i) stream16[i] = silence;
     }
    gBufferBytePosition += len; // Always advance by requested len for continuous playback
    if (gBufferBytePosition >= gBufferByteSize) gBufferBytePosition = 0; // Ensure loop wraps correctly
}


/* --- Test Signal Logic --- */
void generateTestSignal(TestScenario scenario, uint8_t *buffer, uint32_t *totalSamples) {
    int offsetSamples = 0;
    *totalSamples = 0;
    uint32_t maxBufferSamples = 20 * SAMP_FREQ; // Assuming 20 sec max buffer allocated

    printf("\n--- Generating Test Signal ---\n");
    // Initialize buffer to silence
    memset(buffer, 128, maxBufferSamples * sizeof(uint16_t));

    switch (scenario) {
    case TEST_CONSTANT_SPEED: { // 300 Hz
        printf("Scenario: Constant Speed (300 Hz)\n");
        genSineU16(300.0, 10000, 30000, buffer, offsetSamples);
        *totalSamples = (10000 * SAMP_FREQ) / 1000;
        break;
    }
    case TEST_ACCELERATION: { // 300 -> 500 Hz
        printf("Scenario: Acceleration (300 -> 500 Hz over 8s)\n");
        genChirpU16(300.0, 500.0, 8000, 30000, buffer, offsetSamples);
        *totalSamples = (8000 * SAMP_FREQ) / 1000;
        break;
    }
    case TEST_DECELERATION: { // 500 -> 300 Hz
        printf("Scenario: Deceleration (500 -> 300 Hz over 8s)\n");
        genChirpU16(500.0, 300.0, 8000, 30000, buffer, offsetSamples);
        *totalSamples = (8000 * SAMP_FREQ) / 1000;
        break;
    }
    case TEST_WITH_FAULT: { // 420 Hz + 3kHz (Needs addSineU16 implemented)
        printf("Scenario: Constant Speed (420 Hz) + Fault (3 kHz bursts)\n");
        printf("NOTE: Requires addSineU16 to be fully implemented.\n");
        uint32_t baseDuration = 10000; float baseFreq = 420.0;
        genSineU16(baseFreq, baseDuration, 30000, buffer, 0);
        // Add bursts using placeholder/ unimplemented addSineU16
        // int numBursts = baseDuration / 100;
        // for (int i = 0; i < numBursts; i++) {
        //     int burstOffset = (i * 100 * SAMP_FREQ) / 1000;
        //     addSineU16(3000.0, 100, 12000, buffer, burstOffset);
        // }
        *totalSamples = (baseDuration * SAMP_FREQ) / 1000;
        break;
    }
    case TEST_START_STOP: { // 0 -> 300 -> 300 -> 0 -> 0 Hz
        printf("Scenario: Start -> Run (300 Hz) -> Stop\n");
        uint32_t silenceDur=1000, accelDur=2000, runDur=3000, decelDur=2000, stopDur=2000;
        offsetSamples += (silenceDur * SAMP_FREQ) / 1000;
        genChirpU16(0.0, 150.0, accelDur, 30000, buffer, offsetSamples);
        offsetSamples += (accelDur * SAMP_FREQ) / 1000;
        genSineU16(150.0, runDur, 30000, buffer, offsetSamples);
        offsetSamples += (runDur * SAMP_FREQ) / 1000;
        genChirpU16(150.0, 0.0, decelDur, 30000, buffer, offsetSamples);
        offsetSamples += (decelDur * SAMP_FREQ) / 1000;
        offsetSamples += (stopDur * SAMP_FREQ) / 1000;
        *totalSamples = offsetSamples;
        break;
    }
     case TEST_MULTIPLE_HARMONICS: { // 80 + 160 + 240 Hz (Needs addSineU16)
        printf("Scenario: Multiple Harmonics (80 + 160 + 240 Hz)\n");
        printf("NOTE: Requires addSineU16 to be fully implemented.\n");
        uint32_t duration = 10000;
        genSineU16(80.0, duration, 20000, buffer, 0);
        // addSineU16(160.0, duration, 15000, buffer, 0);
        // addSineU16(240.0, duration, 10000, buffer, 0);
        *totalSamples = (duration * SAMP_FREQ) / 1000;
        break;
    }
    default:
        printf("Warning: Unknown scenario.\n"); break;
    }

    // Ensure totalSamples does not exceed buffer capacity
    if (*totalSamples > maxBufferSamples) {
         fprintf(stderr, "Error: Generated signal (%u samples) exceeds max buffer size (%u samples).\n",
                 *totalSamples, maxBufferSamples);
         *totalSamples = 0; // Indicate error
    } else {
        printf("Signal generated: %u samples (%.2f seconds)\n", *totalSamples, (float)*totalSamples / SAMP_FREQ);
    }
     printf("-------------------------------\n\n");
}

/* --- Cleanup & Signal Handler --- */
void cleanup() {
    printf("\nShutting down signal generator...\n");
    if (playbackDeviceId != 0) {
        SDL_PauseAudioDevice(playbackDeviceId, SDL_TRUE);
        SDL_CloseAudioDevice(playbackDeviceId);
    }
    if (gPlaybackBuffer != NULL) free(gPlaybackBuffer);
    SDL_Quit();
    printf("Cleanup done.\n");
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        // cleanup() is called by atexit, just need to exit
        printf("\nCtrl+C detected, exiting.\n");
        exit(0);
    }
}

/* --- Main --- */
int main(int argc, char **argv) {
    TestScenario scenario = TEST_CONSTANT_SPEED;
    if (argc > 1) {
        int scenario_arg = atoi(argv[1]);
        if (scenario_arg >= 0 && scenario_arg <= TEST_MULTIPLE_HARMONICS) {
            scenario = (TestScenario)scenario_arg;
        } else {
             printf("Warning: Invalid scenario '%s'. Using default 0.\n", argv[1]);
        }
    } else { /* Print usage */ } // Simplified usage print

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError()); return 1;
    }
    signal(SIGINT, handle_signal); atexit(cleanup);

    Uint32 maxBufferByteSize = 20 * SAMP_FREQ * sizeof(uint16_t);
    gPlaybackBuffer = (Uint8 *)malloc(maxBufferByteSize);
    if (!gPlaybackBuffer) { perror("malloc failed"); return 1; }

    uint32_t totalSamples = 0;
    generateTestSignal(scenario, gPlaybackBuffer, &totalSamples);
    gBufferByteSize = totalSamples * sizeof(uint16_t);
    if (gBufferByteSize == 0) return 1; // Exit if signal generation failed

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = SAMP_FREQ; desired.format = FORMAT; desired.channels = MONO;
    desired.samples = ABUFSIZE_SAMPLES; desired.callback = audioPlaybackCallback;

    playbackDeviceId = SDL_OpenAudioDevice(NULL, SDL_FALSE, &desired, &obtained, 0); // Allow no changes
    if (playbackDeviceId == 0) {
        fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError()); return 1;
    }
    printf("Opened playback device (Samples: %d).\n", obtained.samples);

    printf("Playing scenario %d... Press Ctrl+C to stop.\n", scenario);
    gBufferBytePosition = 0;
    SDL_PauseAudioDevice(playbackDeviceId, SDL_FALSE); // Start playback

    while (1) { SDL_Delay(1000); } // Keep main alive

    return 0; // Unreachable
}