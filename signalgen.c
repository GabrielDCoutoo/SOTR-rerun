#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>

#define MONO 1
#define SAMP_FREQ 44100
#define FORMAT AUDIO_U16
#define ABUFSIZE_SAMPLES 4096

// Test scenarios
typedef enum {
    TEST_CONSTANT_SPEED,      // Motor at constant 100 Hz
    TEST_ACCELERATION,        // Speed ramps from 50 to 200 Hz
    TEST_DECELERATION,        // Speed ramps from 200 to 50 Hz
    TEST_WITH_FAULT,          // Constant speed + high-freq noise (bearing fault)
    TEST_START_STOP,          // Motor starts, runs, stops
    TEST_MULTIPLE_HARMONICS   // Complex signal with multiple frequencies
} TestScenario;

// Playback data
SDL_AudioDeviceID playbackDeviceId = 0;
SDL_AudioSpec gReceivedPlaybackSpec;
Uint8 *gPlaybackBuffer = NULL;
Uint32 gBufferBytePosition = 0;
Uint32 gBufferByteSize = 0;

/* *************************************************************************************
 * Generate a sine wave
 * freq: frequency in Hz
 * durationMS: duration in milliseconds
 * amp: amplitude (0 to 65535 for AUDIO_U16)
 * buffer: output buffer
 * offset: starting offset in buffer
 * ***********************************************************************************/
void genSineU16(float freq, uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset)
{
    int i = 0, nSamples = 0;
    float sinArgK = 2 * M_PI * freq;
    uint16_t *bufU16 = (uint16_t *)buffer;
    
    nSamples = ((float)durationMS / 1000) * SAMP_FREQ;
    
    for (i = 0; i < nSamples; i++) {
        bufU16[offset + i] = 32768 + (amp / 2) * sin((sinArgK * i) / SAMP_FREQ);
    }
}

/* *************************************************************************************
 * Generate a frequency sweep (chirp) - CORRECTED VERSION
 * ***********************************************************************************/
void genChirpU16(float startFreq, float endFreq, uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset)
{
    int i = 0, nSamples = 0;
    uint16_t *bufU16 = (uint16_t *)buffer;
    
    nSamples = ((float)durationMS / 1000) * SAMP_FREQ;
    
    float currentFreq;
    double phase = 0.0; // Use a double for phase and accumulate it

    for (i = 0; i < nSamples; i++) {
        // Calculate the frequency at this sample
        currentFreq = startFreq + (endFreq - startFreq) * i / nSamples;
        
        // Add the current phase step
        phase += 2.0 * M_PI * currentFreq / SAMP_FREQ;

        // Generate the sample
        bufU16[offset + i] = 32768 + (amp / 2) * sin(phase);
    }
    
    // Optional: Reset phase if it gets too large to prevent overflow
    // if (phase > 2.0 * M_PI * 1000000.0) phase = 0.0; 
}

/* *************************************************************************************
 * Add white noise to buffer
 * ***********************************************************************************/
void addNoiseU16(uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset)
{
    int i = 0, nSamples = 0;
    uint16_t *bufU16 = (uint16_t *)buffer;
    
    nSamples = ((float)durationMS / 1000) * SAMP_FREQ;
    
    for (i = 0; i < nSamples; i++) {
        int noise = (rand() % amp) - (amp / 2);
        bufU16[offset + i] = (bufU16[offset + i] + noise) & 0xFFFF;
    }
}

/* *************************************************************************************
 * Audio playback callback
 * ***********************************************************************************/
void audioPlaybackCallback(void *userdata, Uint8 *stream, int len)
{
    if (gBufferBytePosition >= gBufferByteSize) {
        // Loop back to beginning
        gBufferBytePosition = 0;
    }
    
    memcpy(stream, &gPlaybackBuffer[gBufferBytePosition], len);
    gBufferBytePosition += len;
}

/* new: add sine to buffer (adds to existing samples, clamps, keeps unsigned 16-bit format) */
void addSineU16(float freq, uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset)
{
    int i = 0, nSamples = 0;
    uint16_t *bufU16 = (uint16_t *)buffer;
    nSamples = ((float)durationMS / 1000) * SAMP_FREQ;

    for (i = 0; i < nSamples; i++) {
        /* convert current unsigned sample to signed centered */
        int32_t current = (int32_t)bufU16[offset + i] - 32768;
        /* compute sample to add */
        float phase = 2.0f * M_PI * freq * i / SAMP_FREQ;
        int32_t add = (int32_t)roundf((amp / 2.0f) * sinf(phase));
        int32_t summed = current + add;
        /* clamp to signed 16-bit range */
        if (summed > 32767) summed = 32767;
        if (summed < -32768) summed = -32768;
        /* store back as unsigned */
        bufU16[offset + i] = (uint16_t)(summed + 32768);
    }
}

/* *************************************************************************************
 * Generate test signal based on scenario
 * ***********************************************************************************/
void generateTestSignal(TestScenario scenario, uint8_t *buffer, uint32_t *totalSamples)
{
    int offset = 0;
    *totalSamples = 0;
    
    printf("\n===========================================\n");
    printf("Generating test signal...\n");
    printf("===========================================\n");
    
    switch (scenario) {
        case TEST_CONSTANT_SPEED:
            printf("Scenario: Constant Speed (100 Hz)\n");
            printf("Expected: Speed = 100 Hz, Direction = STABLE\n");
            genSineU16(800.0, 10000, 30000, buffer, offset);  // fixed: 100 Hz
            *totalSamples = (10000 * SAMP_FREQ) / 1000;
            break;
            
        case TEST_ACCELERATION:
            printf("Scenario: Acceleration (50 Hz → 200 Hz over 8 seconds)\n");
            printf("Expected: Direction = FORWARD (accelerating)\n");
            genChirpU16(400.0, 800.0, 8000, 30000, buffer, offset);
            *totalSamples = (8000 * SAMP_FREQ) / 1000;
            break;
            
        case TEST_DECELERATION:
            printf("Scenario: Deceleration (200 Hz → 50 Hz over 8 seconds)\n");
            printf("Expected: Direction = REVERSE (decelerating)\n");
            genChirpU16(800.0,400.0, 8000, 30000, buffer, offset);
            *totalSamples = (8000 * SAMP_FREQ) / 1000;
            break;
            
        case TEST_WITH_FAULT:
            printf("Scenario: Constant Speed + Bearing Fault\n");
            printf("Expected: Speed = 120 Hz, Issue = FAULT DETECTED\n");
            // Base frequency (motor speed)
            genSineU16(420.0, 10000, 30000, buffer, offset);
            // Add high-frequency noise (bearing fault at 3 kHz) as additions
            offset = 0;
            for (int i = 0; i < 10000 / 100; i++) {
                addSineU16(3000.0, 100, 12000, buffer, offset);  // ADD burst (do not overwrite)
                offset += (100 * SAMP_FREQ) / 1000;
            }
            *totalSamples = (10000 * SAMP_FREQ) / 1000;
            break;

        case TEST_START_STOP:
            printf("Scenario: Start → Run → Stop\n");
            printf("Expected: STOP → FORWARD → STABLE → REVERSE → STOP\n");
            // Stop (silence) -- set to center value for AUDIO_U16
            {
                uint16_t *bufU16 = (uint16_t *)buffer;
                int n = (1000 * SAMP_FREQ) / 1000;
                for (int i = 0; i < n; i++) bufU16[i] = 32768;
            }
            offset = (1000 * SAMP_FREQ) / 1000;
            
            // Start (acceleration)
            genChirpU16(0.0, 400.0, 2000, 30000, buffer, offset);
            offset += (2000 * SAMP_FREQ) / 1000;
            
            // Run (constant speed)
            genSineU16(400.0, 3000, 30000, buffer, offset);
            offset += (3000 * SAMP_FREQ) / 1000;
            
            // Stop (deceleration)
            genChirpU16(400.0, 0.0, 2000, 30000, buffer, offset);
            offset += (2000 * SAMP_FREQ) / 1000;
            
            // Stopped (silence)
            {
                uint16_t *bufU16 = (uint16_t *)buffer + offset;
                int n = (2000 * SAMP_FREQ) / 1000;
                for (int i = 0; i < n; i++) bufU16[i] = 32768;
            }
            offset += (2000 * SAMP_FREQ) / 1000;
            
            *totalSamples = offset;
            break;
            
        case TEST_MULTIPLE_HARMONICS:
            printf("Scenario: Multiple Harmonics (80, 160, 240 Hz)\n");
            printf("Expected: Speed should detect strongest frequency\n");
            // initialize to silence (center)
            {
                uint16_t *bufU16 = (uint16_t *)buffer;
                int n = (10000 * SAMP_FREQ) / 1000;
                for (int i = 0; i < n; i++) bufU16[i] = 32768;
            }
            addSineU16(80.0, 10000, 20000, buffer, 0);
            addSineU16(160.0, 10000, 15000, buffer, 0);
            addSineU16(240.0, 10000, 10000, buffer, 0);
            *totalSamples = (10000 * SAMP_FREQ) / 1000;
            break;
    }
    
    printf("Signal generated: %u samples (%.2f seconds)\n", *totalSamples, (float)*totalSamples / SAMP_FREQ);
    printf("===========================================\n\n");
}

/* *************************************************************************************
 * Cleanup and Signal Handler
 * ***********************************************************************************/

void cleanup() {
    printf("\nShutting down signal generator...\n");
    if (playbackDeviceId != 0) {
        SDL_PauseAudioDevice(playbackDeviceId, SDL_TRUE);
        SDL_CloseAudioDevice(playbackDeviceId);
    }
    if (gPlaybackBuffer != NULL) {
        free(gPlaybackBuffer);
    }
    SDL_Quit();
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        cleanup();
        exit(0);
    }
}

/* *************************************************************************************
 * Main
 * ***********************************************************************************/
int main(int argc, char **argv)
{
    printf("DEBUG: Program started.\n");
    fflush(stdout);
    
    TestScenario scenario = TEST_CONSTANT_SPEED;
    
    // Parse command line argument
    if (argc > 1) {
        int s = atoi(argv[1]);
        if (s >= 0 && s <= 5) {
            scenario = (TestScenario)s;
        }
    } else {
        printf("\n===========================================\n");
        printf("Signal Generator for rtsounds Testing\n");
        printf("===========================================\n");
        printf("Usage: %s [scenario]\n\n", argv[0]);
        printf("Scenarios:\n");
        printf("  0 - Constant Speed (100 Hz)\n");
        printf("  1 - Acceleration (50→200 Hz)\n");
        printf("  2 - Deceleration (200→50 Hz)\n");
        printf("  3 - With Bearing Fault (120 Hz + 3kHz noise)\n");
        printf("  4 - Start-Stop Sequence\n");
        printf("  5 - Multiple Harmonics\n");
        printf("\nDefaulting to scenario 0\n");
        printf("===========================================\n");
        fflush(stdout);
    }
    
    printf("DEBUG: Initializing SDL Audio...\n");
    fflush(stdout);
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("FATAL ERROR: SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        fflush(stdout);
        return 1;
    }

    // Register signal handler for Ctrl+C
    signal(SIGINT, handle_signal);
    
    printf("DEBUG: SDL Audio Initialized.\n");
    fflush(stdout);
    
    // Allocate buffer (20 seconds max)
    gBufferByteSize = 20 * SAMP_FREQ * sizeof(uint16_t);
    gPlaybackBuffer = (Uint8 *)malloc(gBufferByteSize);
    memset(gPlaybackBuffer, 0, gBufferByteSize);
    
    printf("DEBUG: Buffer allocated.\n");
    fflush(stdout);

    // Generate test signal
    uint32_t totalSamples = 0;
    generateTestSignal(scenario, gPlaybackBuffer, &totalSamples);
    gBufferByteSize = totalSamples * sizeof(uint16_t);
    
    printf("DEBUG: Signal generated.\n");
    fflush(stdout);

    // Set up playback
    SDL_AudioSpec desiredPlaybackSpec;
    SDL_zero(desiredPlaybackSpec);
    desiredPlaybackSpec.freq = SAMP_FREQ;
    desiredPlaybackSpec.format = FORMAT;
    desiredPlaybackSpec.channels = MONO;
    desiredPlaybackSpec.samples = ABUFSIZE_SAMPLES;
    desiredPlaybackSpec.callback = audioPlaybackCallback;
    
    printf("DEBUG: Opening audio device...\n");
    fflush(stdout);
    
    // Open playback device
    playbackDeviceId = SDL_OpenAudioDevice(NULL, SDL_FALSE, &desiredPlaybackSpec, &gReceivedPlaybackSpec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    
    if (playbackDeviceId == 0) {
        printf("FATAL ERROR: Failed to open playback device! SDL Error: %s\n", SDL_GetError());
        fflush(stdout);
        SDL_Quit(); // Quit SDL before returning
        return 1;
    }
    
    printf("DEBUG: Audio device opened successfully.\n");
    fflush(stdout);
    
    printf("Playing test signal... (will loop)\n");
    printf("Press Ctrl+C to stop\n\n");
    fflush(stdout);
    
    // Start playback
    gBufferBytePosition = 0;
    SDL_PauseAudioDevice(playbackDeviceId, SDL_FALSE);
    
    printf("DEBUG: Playback started. Entering infinite loop.\n");
    fflush(stdout);
    
    // Keep playing until user stops
    while (1) {
        SDL_Delay(1000);
    }
    
    // Cleanup
    printf("DEBUG: Loop exited. Cleaning up.\n");
    fflush(stdout);
    SDL_CloseAudioDevice(playbackDeviceId);
    free(gPlaybackBuffer);
    SDL_Quit();
    
    return 0;
}