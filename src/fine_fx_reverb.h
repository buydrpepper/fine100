//NOTE: TRANSLATION OF FREEVERB BY GEMINI 
#ifndef FINE_REVERB_H
#define FINE_REVERB_H

#include <stdint.h> // For int16_t
#include <stddef.h> // For size_t

// User-requested type definition
typedef int16_t i16;

// Macro for killing denormalled numbers, from denormals.h
#define undenormalise(sample) if(((*(unsigned int*)&sample)&0x7f800000)==0) sample=0.0f

// --- Tunings from tuning.h (Left Channel) ---
//
#define NUM_COMBS       8
#define NUM_ALLPASSES   4

// Comb filter buffer sizes (Left channel)
#define combtuningL1    1116
#define combtuningL2    1188
#define combtuningL3    1277
#define combtuningL4    1356
#define combtuningL5    1422
#define combtuningL6    1491
#define combtuningL7    1557
#define combtuningL8    1617

// Allpass filter buffer sizes (Left channel)
#define allpasstuningL1 556
#define allpasstuningL2 441
#define allpasstuningL3 341
#define allpasstuningL4 225

// --- Filter Structures ---

/**
 * @brief State for a single comb filter.
 * Adapted from comb.hpp
 */
typedef struct {
    float   *buffer;
    int32_t bufsize;
    int32_t bufidx;
    float   filterstore;
    float   feedback;
    float   damp1;
    float   damp2;
} comb_filter;

/**
 * @brief State for a single allpass filter.
 * Adapted from allpass.hpp
 */
typedef struct {
    float   *buffer;
    int32_t bufsize;
    int32_t bufidx;
    float   feedback;
} allpass_filter;

/**
 * @brief Main reverb model structure.
 * This holds all state, parameters, and delay buffers for the reverb.
 * Adapted from revmodel.hpp
 */
typedef struct {
    // Normalized parameters (0.0 to 1.0)
    float roomsize;
    float damp;
    float wet;
    float dry;

    // Internal calculated values
    float gain;
    float wet_scaled;
    float dry_scaled;

    // Filter instances
    comb_filter     combs[NUM_COMBS];
    allpass_filter  allpasses[NUM_ALLPASSES];

    // --- Buffers ---
    // These are embedded directly to avoid dynamic allocation.
    //
    
    // Comb buffers
    float buf_comb1[combtuningL1];
    float buf_comb2[combtuningL2];
    float buf_comb3[combtuningL3];
    float buf_comb4[combtuningL4];
    float buf_comb5[combtuningL5];
    float buf_comb6[combtuningL6];
    float buf_comb7[combtuningL7];
    float buf_comb8[combtuningL8];

    // Allpass buffers
    float buf_allpass1[allpasstuningL1];
    float buf_allpass2[allpasstuningL2];
    float buf_allpass3[allpasstuningL3];
    float buf_allpass4[allpasstuningL4];

} fine_reverb_model;


// --- Public Functions ---

/**
 * @brief Initializes the reverb model structure.
 * Call this once before using the reverb.
 * Sets up buffers, mutes them, and applies initial default parameters.
 * @param rvb Pointer to the reverb model to initialize.
 */
void reverb_init(fine_reverb_model *rvb);

/**
 * @brief Sets the reverb parameters.
 * All parameters are normalized to a 0.0 to 1.0 range.
 *
 * @param rvb       Pointer to the reverb model.
 * @param roomsize  Controls reverb time (0.0 to 1.0).
 * @param damp      Controls high-frequency damping (0.0 to 1.0).
 * @param wet       Controls the amount of wet (reverb) signal (0.0 to 1.0).
 * @param dry       Controls the amount of dry (original) signal (0.0 to 1.0).
 */
void reverb_set_params(fine_reverb_model *rvb, float roomsize, float damp, float wet, float dry);

/**
 * @brief Processes a buffer of audio data.
 * This is the function you requested. It applies reverb to the
 * data in-place.
 *
 * @param data  Pointer to the buffer of 16-bit signed samples.
 * @param sz    Number of samples in the buffer.
 * @param rvb   Pointer to the initialized reverb model.
 */
void fine_fx_reverb(i16 *const data, size_t const sz, fine_reverb_model *rvb);

#endif // FINE_REVERB_H
