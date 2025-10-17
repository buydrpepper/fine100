#include "fine_fx_reverb.h"
#include <string.h> // For memset

// --- Parameter constants from tuning.h ---
//
const float fixedgain     = 0.015f;
const float scalewet      = 3.0f;
const float scaledry      = 2.0f;
const float scaledamp     = 0.4f;
const float scaleroom     = 0.28f;
const float offsetroom    = 0.7f;
const float initialroom   = 0.5f;
const float initialdamp   = 0.5f;
const float initialwet    = 1.0f / scalewet;
const float initialdry    = 0.0f;

// --- Private Functions ---

/**
 * @brief Internal function to update filter coefficients from normalized parameters.
 * Based on revmodel::update()
 */
static void reverb_update(fine_reverb_model *rvb) {
    // Calculate internal gains
    rvb->wet_scaled = rvb->wet * scalewet;
    rvb->dry_scaled = rvb->dry * scaledry;
    rvb->gain = fixedgain; // Assuming mode is not 'freeze'

    // Calculate filter coefficients
    float roomsize1 = (rvb->roomsize * scaleroom) + offsetroom;
    float damp1 = rvb->damp * scaledamp;
    float damp2 = 1.0f - damp1;

    for (int i = 0; i < NUM_COMBS; i++) {
        rvb->combs[i].feedback = roomsize1;
        rvb->combs[i].damp1 = damp1;
        rvb->combs[i].damp2 = damp2;
    }
}

/**
 * @brief Processes one sample through a comb filter.
 * C translation of comb::process()
 */
static inline float comb_process(comb_filter *c, float input) {
    float output;

    output = c->buffer[c->bufidx];
    undenormalise(output); //

    c->filterstore = (output * c->damp2) + (c->filterstore * c->damp1);
    undenormalise(c->filterstore); //

    c->buffer[c->bufidx] = input + (c->filterstore * c->feedback);

    if (++(c->bufidx) >= c->bufsize) {
        c->bufidx = 0;
    }
    return output;
}

/**
 * @brief Processes one sample through an allpass filter.
 * C translation of allpass::process()
 */
static inline float allpass_process(allpass_filter *a, float input) {
    float output, bufout;

    bufout = a->buffer[a->bufidx];
    undenormalise(bufout); //

    output = -input + bufout;
    a->buffer[a->bufidx] = input + (bufout * a->feedback);

    if (++(a->bufidx) >= a->bufsize) {
        a->bufidx = 0;
    }
    return output;
}


// --- Public Function Implementations ---

void reverb_init(fine_reverb_model *rvb) {
    // Zero out the entire structure. This mutes all buffers
    // and sets all indices, filter stores, etc., to 0.
    memset(rvb, 0, sizeof(fine_reverb_model));

    // --- Link comb filters to their buffers ---
    //
    rvb->combs[0].buffer = rvb->buf_comb1; rvb->combs[0].bufsize = combtuningL1;
    rvb->combs[1].buffer = rvb->buf_comb2; rvb->combs[1].bufsize = combtuningL2;
    rvb->combs[2].buffer = rvb->buf_comb3; rvb->combs[2].bufsize = combtuningL3;
    rvb->combs[3].buffer = rvb->buf_comb4; rvb->combs[3].bufsize = combtuningL4;
    rvb->combs[4].buffer = rvb->buf_comb5; rvb->combs[4].bufsize = combtuningL5;
    rvb->combs[5].buffer = rvb->buf_comb6; rvb->combs[5].bufsize = combtuningL6;
    rvb->combs[6].buffer = rvb->buf_comb7; rvb->combs[6].bufsize = combtuningL7;
    rvb->combs[7].buffer = rvb->buf_comb8; rvb->combs[7].bufsize = combtuningL8;

    // --- Link allpass filters to their buffers ---
    //
    rvb->allpasses[0].buffer = rvb->buf_allpass1; rvb->allpasses[0].bufsize = allpasstuningL1;
    rvb->allpasses[1].buffer = rvb->buf_allpass2; rvb->allpasses[1].bufsize = allpasstuningL2;
    rvb->allpasses[2].buffer = rvb->buf_allpass3; rvb->allpasses[2].bufsize = allpasstuningL3;
    rvb->allpasses[3].buffer = rvb->buf_allpass4; rvb->allpasses[3].bufsize = allpasstuningL4;

    // Set initial allpass feedback (fixed value)
    for (int i = 0; i < NUM_ALLPASSES; i++) {
        rvb->allpasses[i].feedback = 0.5f;
    }

    // Set initial default parameters
    rvb->roomsize = initialroom;
    rvb->damp = initialdamp;
    rvb->wet = initialwet;
    rvb->dry = initialdry;

    // Calculate initial coefficients
    reverb_update(rvb);
}

void reverb_set_params(fine_reverb_model *rvb, float roomsize, float damp, float wet, float dry) {
    rvb->roomsize = roomsize;
    rvb->damp = damp;
    rvb->wet = wet;
    rvb->dry = dry;
    reverb_update(rvb);
}

void fine_fx_reverb(i16 *const data, size_t const sz, fine_reverb_model *rvb) {
    
    // Process sample by sample
    for (size_t i = 0; i < sz; i++) {
        
        // --- 1. Convert i16 to float ---
        // Scale -32768..32767 to -1.0..1.0
        const float in_sample = (float)data[i] * (1.0f / 32768.0f);
        
        float out_sample = 0.0f;
        float input = in_sample * rvb->gain;

        // --- 2. Process Reverb Logic ---
        // Based on revmodel::processreplace()
        
        // Accumulate comb filters in parallel
        for (int j = 0; j < NUM_COMBS; j++) {
            out_sample += comb_process(&rvb->combs[j], input);
        }

        // Feed through allpasses in series
        for (int j = 0; j < NUM_ALLPASSES; j++) {
            out_sample = allpass_process(&rvb->allpasses[j], out_sample);
        }

        // --- 3. Mix wet and dry signals ---
        // Mono equivalent of the stereo mix
        out_sample = out_sample * rvb->wet_scaled + in_sample * rvb->dry_scaled;

        // --- 4. Convert float to i16 ---
        // Scale -1.0..1.0 to -32767..32767 (with saturation)
        out_sample *= 32767.0f;

        // Hard clipping
        if (out_sample > 32767.0f) {
            out_sample = 32767.0f;
        } else if (out_sample < -32768.0f) {
            out_sample = -32768.0f;
        }

        // Write back to the buffer
        data[i] = (i16)out_sample;
    }
}
