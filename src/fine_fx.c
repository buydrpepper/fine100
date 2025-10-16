#include "fine_definitions.h"

#include <stdlib.h>
#include "fine_fx.h"
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

