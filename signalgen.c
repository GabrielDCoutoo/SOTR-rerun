#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

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
 * Generate a frequency sweep (chirp)
 * startFreq: starting frequency in Hz
 * endFreq: ending frequency in Hz
 * durationMS: duration in milliseconds
 * amp: amplitude
 * buffer: output buffer
 * offset: starting offset in buffer
 * ***********************************************************************************/
void genChirpU16(float startFreq, float endFreq, uint32_t durationMS, uint16_t amp, uint8_t *buffer, int offset)
{
    int i = 0, nSamples = 0;
    uint16_t *bufU16 = (uint16_t *)buffer;
    
    nSamples = ((float)durationMS / 1000) * SAMP_FREQ;
    float freqRate = (endFreq - startFreq) / nSamples;
    
    for (i = 0; i < nSamples; i++) {
        float currentFreq = startFreq + freqRate * i;
        float phase = 2 * M_PI * currentFreq * i / SAMP_FREQ;
        bufU16[offset + i] = 32768 + (amp / 2) * sin(phase);
    }
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
            genSineU16(100.0, 10000, 30000, buffer, offset);  // 10 seconds at 100 Hz
            *totalSamples = (10000 * SAMP_FREQ) / 1000;
            break;
            
        case TEST_ACCELERATION:
            printf("Scenario: Acceleration (50 Hz → 200 Hz over 8 seconds)\n");
            printf("Expected: Direction = FORWARD (accelerating)\n");
            genChirpU16(50.0, 200.0, 8000, 30000, buffer, offset);
            *totalSamples = (8000 * SAMP_FREQ) / 1000;
            break;
            
        case TEST_DECELERATION:
            printf("Scenario: Deceleration (200 Hz → 50 Hz over 8 seconds)\n");
            printf("Expected: Direction = REVERSE (decelerating)\n");
            genChirpU16(200.0, 50.0, 8000, 30000, buffer, offset);
            *totalSamples = (8000 * SAMP_FREQ) / 1000;
            break;
            
        case TEST_WITH_FAULT:
            printf("Scenario: Constant Speed + Bearing Fault\n");
            printf("Expected: Speed = 120 Hz, Issue = FAULT DETECTED\n");
            // Base frequency (motor speed)
            genSineU16(120.0, 10000, 30000, buffer, offset);
            // Add high-frequency noise (bearing fault at 3 kHz)
            offset = 0;
            for (int i = 0; i < 10000 / 100; i++) {
                genSineU16(3000.0, 100, 12000, buffer, offset);  // 20% amplitude of base
                offset += (100 * SAMP_FREQ) / 1000;
            }
            *totalSamples = (10000 * SAMP_FREQ) / 1000;
            break;
            
        case TEST_START_STOP:
            printf("Scenario: Start → Run → Stop\n");
            printf("Expected: STOP → FORWARD → STABLE → REVERSE → STOP\n");
            // Stop (silence)
            memset(buffer, 128, (1000 * SAMP_FREQ / 1000) * sizeof(uint16_t));
            offset = (1000 * SAMP_FREQ) / 1000;
            
            // Start (acceleration)
            genChirpU16(0.0, 150.0, 2000, 30000, buffer, offset);
            offset += (2000 * SAMP_FREQ) / 1000;
            
            // Run (constant speed)
            genSineU16(150.0, 3000, 30000, buffer, offset);
            offset += (3000 * SAMP_FREQ) / 1000;
            
            // Stop (deceleration)
            genChirpU16(150.0, 0.0, 2000, 30000, buffer, offset);
            offset += (2000 * SAMP_FREQ) / 1000;
            
            // Stopped
            memset(buffer + offset * sizeof(uint16_t), 128, (2000 * SAMP_FREQ / 1000) * sizeof(uint16_t));
            offset += (2000 * SAMP_FREQ) / 1000;
            
            *totalSamples = offset;
            break;
            
        case TEST_MULTIPLE_HARMONICS:
            printf("Scenario: Multiple Harmonics (80, 160, 240 Hz)\n");
            printf("Expected: Speed should detect strongest frequency\n");
            genSineU16(80.0, 10000, 20000, buffer, 0);
            genSineU16(160.0, 10000, 15000, buffer, 0);  // Overlays on same buffer
            genSineU16(240.0, 10000, 10000, buffer, 0);
            *totalSamples = (10000 * SAMP_FREQ) / 1000;
            break;
    }
    
    printf("Signal generated: %u samples (%.2f seconds)\n", *totalSamples, (float)*totalSamples / SAMP_FREQ);
    printf("===========================================\n\n");
}

/* *************************************************************************************
 * Main
 * ***********************************************************************************/
int main(int argc, char **argv)
{
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
    }
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return 1;
    }
    
    // Allocate buffer (20 seconds max)
    gBufferByteSize = 20 * SAMP_FREQ * sizeof(uint16_t);
    gPlaybackBuffer = (Uint8 *)malloc(gBufferByteSize);
    memset(gPlaybackBuffer, 0, gBufferByteSize);
    
    // Generate test signal
    uint32_t totalSamples = 0;
    generateTestSignal(scenario, gPlaybackBuffer, &totalSamples);
    gBufferByteSize = totalSamples * sizeof(uint16_t);
    
    // Set up playback
    SDL_AudioSpec desiredPlaybackSpec;
    SDL_zero(desiredPlaybackSpec);
    desiredPlaybackSpec.freq = SAMP_FREQ;
    desiredPlaybackSpec.format = FORMAT;
    desiredPlaybackSpec.channels = MONO;
    desiredPlaybackSpec.samples = ABUFSIZE_SAMPLES;
    desiredPlaybackSpec.callback = audioPlaybackCallback;
    
    // Open playback device
    playbackDeviceId = SDL_OpenAudioDevice(NULL, SDL_FALSE, &desiredPlaybackSpec, &gReceivedPlaybackSpec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    
    if (playbackDeviceId == 0) {
        printf("Failed to open playback device! SDL Error: %s\n", SDL_GetError());
        return 1;
    }
    
    printf("Playing test signal... (will loop)\n");
    printf("Press Ctrl+C to stop\n\n");
    
    // Start playback
    gBufferBytePosition = 0;
    SDL_PauseAudioDevice(playbackDeviceId, SDL_FALSE);
    
    // Keep playing until user stops
    while (1) {
        SDL_Delay(1000);
    }
    
    // Cleanup
    SDL_CloseAudioDevice(playbackDeviceId);
    free(gPlaybackBuffer);
    SDL_Quit();
    
    return 0;
}