// fine_fx_compress.c
// NOTE: CHATGPT WROTE THIS FILE
// Fast in-place compressor for int16 audio buffers.
//
// Usage example:
//   fine_fx_compress(samples, n_samples, sample_rate,
//                    8000.0f,   // threshold (linear, same scale as int16 samples, e.g. 32767 max)
//                    4.0f,      // ratio (>=1.0)
//                    5.0f,      // attack_ms
//                    80.0f,     // release_ms
//                    1.0f);     // makeup gain (linear multiplier)
//
// Build with -O3. If target has FPU, enabling -mfpu/--float-abi=hard helps.

#include "fine_fx.h"
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#ifndef M_EPSILON_F
#define M_EPSILON_F 1e-12f
#endif

static inline float ms_to_coeff(float ms, unsigned sample_rate)
{
    // coefficient for one-pole smoothing: alpha = exp(-1/(tau*fs))
    // where tau = ms/1000.0
    if (ms <= 0.0f) return 0.0f; // instant
    float tau = ms * 0.001f;
    float denom = tau * (float)sample_rate;
    if (denom <= 0.0f) return 0.0f;
    return expf(-1.0f / denom);
}

// data : pointer to int16 samples (mono), in-place
// sz   : number of samples
// sample_rate : sample rate in Hz
// threshold   : linear amplitude threshold (use same scale as int16, e.g. 32767 max)
// ratio       : compression ratio (>= 1.0) (e.g. 4.0 for 4:1)
// attack_ms   : attack time in milliseconds
// release_ms  : release time in milliseconds
// makeup_gain : linear multiplier applied to output (1.0 = no make-up)
void fine_fx_compress(int16_t * const data,
                      size_t const sz,
                      unsigned const sample_rate,
                      float const threshold,
                      float const ratio,
                      float const attack_ms,
                      float const release_ms,
                      float const makeup_gain)
{
    if (!data || sz == 0 || sample_rate == 0) return;

    // Quick bypasss: no compression needed
    if (ratio <= 1.000001f || threshold <= 0.0f) {
        // just apply makeup gain (fast path)
        if (fabsf(makeup_gain - 1.0f) < 1e-12f) return;
        for (size_t i = 0; i < sz; ++i) {
            float out = (float)data[i] * makeup_gain;
            if (out > 32767.0f) out = 32767.0f;
            else if (out < -32768.0f) out = -32768.0f;
            data[i] = (int16_t)lrintf(out);
        }
        return;
    }

    // Precompute coefficients
    const float attack_coeff = ms_to_coeff(attack_ms, sample_rate);
    const float release_coeff = ms_to_coeff(release_ms, sample_rate);

    // k = (1 - 1/ratio) used in gain calculation: gain = (env/threshold)^(-k)
    const float inv_ratio = 1.0f / ratio;
    const float k = 1.0f - inv_ratio;

    // Envelope and smoothed gain
    float env = 0.0f;
    float g_smoothed = 1.0f;

    // Small epsilon to avoid divide-by-zero or log(0)
    const float eps = 1e-12f;

    // Precompute 1 - coeff to avoid recomputing in loop
    const float one_minus_attack = 1.0f - attack_coeff;
    const float one_minus_release = 1.0f - release_coeff;

    // Use pointer loop for best optimizer results
    int16_t * restrict p = data;
    size_t n = sz;

    while (n--) {
        // load
        float x = (float)(*p);
        float absx = fabsf(x);

        // envelope follower (attack/release)
        if (absx > env) {
            // attack (respond quickly to increases)
            env = attack_coeff * env + one_minus_attack * absx;
        } else {
            // release (respond more slowly to decreases)
            env = release_coeff * env + one_minus_release * absx;
        }

        // compute desired gain
        float gain = 1.0f;
        if (env > threshold + eps) {
            // ratio-based attenuation in linear domain: gain = (env/threshold)^(-k)
            // compute base = env / threshold
            float base = env / threshold;
            // powf can be costly but only executed when env>threshold
            gain = powf(base, -k);
        } else {
            gain = 1.0f;
        }

        // smooth gain to avoid zipper noise (use release-like smoothing for gain)
        // A simple 1-pole smoother: quick attack for increasing gain reductions can be desirable,
        // but we already have envelope attack; keep it simple and stable:
        g_smoothed = (gain < g_smoothed) ? (attack_coeff * g_smoothed + one_minus_attack * gain)
                                         : (release_coeff * g_smoothed + one_minus_release * gain);

        // apply gain and makeup, then clamp to int16
        float out = x * g_smoothed * makeup_gain;
        if (out > 32767.0f) out = 32767.0f;
        else if (out < -32768.0f) out = -32768.0f;
        *p = (int16_t)lrintf(out);

        ++p;
    }
}

