#include "fine_definitions.h"

#include <stdlib.h>
#include "fine_fx.h"
#include "p99/p99.h"
#include <stddef.h>
#include <math.h>
/* 
 * Fast effects for my FINE project.
 * Note that all filters assume a mono input.
 * */
void fine_fx_amplify(i16 *const data, size_t const sz, float const gain) {
	for(size_t i = 0; i < sz; ++i) {
		float new = data[i]*gain;
		if(new >= INT16_MAX) new = INT16_MAX;
		else if(new <= INT16_MIN) new = INT16_MIN;
		data[i] = new;
	}
}

//Could be expensive, maybe process in chunks
void fine_fx_fade_linear(i16 *const data, size_t const sz, size_t in, size_t out) {
	for(size_t i = 0; i < sz; ++i) {
		int fadein = i<in;
		int fadeout = i>=sz-out;
		if(fadein || fadeout) {
			float const gain = P99_MINOF((float)i/in, (float)(sz-1-i)/out);
			float new = data[i]*gain;
			data[i] = roundf(new);
		}
	}
}




void fine_fx_delay(i16 *const data, size_t const sz, ...);
