#pragma once
#include <stdio.h>
#include "fine_definitions.h"

void fine_fx_amplify(i16 *data, size_t sz, float gain);

void fine_fx_compress(int16_t * data,
                      size_t sz,
                      unsigned sample_rate,
                      float  threshold,
                      float  ratio,
                      float  attack_ms,
                      float  release_ms,
                      float  makeup_gain);

