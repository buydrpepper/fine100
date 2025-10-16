
#include "fine_definitions.h"
#include "fine_log.h"
#include "fine_audio_io.h"
#include "p99/p99.h"
#include <alsa/asoundlib.h>
#include <limits.h>
#include <threads.h>
#include <stdatomic.h>
#include <string.h>

int fine_output_read_until(i16 * const data, size_t const sz, snd_pcm_t *const pcm_out, snd_pcm_hw_params_t *const hw_out, _Atomic(bool) *fade_out) {

	//TODO: check if writing FULL buffer works
	
	snd_pcm_sframes_t left = sz;
	snd_pcm_uframes_t tmp_uframe = 0; 
	if(snd_pcm_hw_params_get_period_size(hw_out, &tmp_uframe, 0)<0) 
		fine_exit("Input period size cannot be read");
	snd_pcm_uframes_t const per_write = tmp_uframe;
	float gain = 1;
	int do_fade = 0;
	while(left > 0 && gain > 0.01) {
		if(!do_fade && atomic_load_explicit(fade_out, memory_order_acquire)) {
			do_fade = 1;
			atomic_store_explicit(fade_out, 0, memory_order_release);
		}
		snd_pcm_sframes_t const towrite = left < per_write? left : per_write;
		snd_pcm_sframes_t written; 
		while((written = snd_pcm_writei(pcm_out, data+(sz-left), towrite)) < 0) {
			switch(written) {
				case -EPIPE:
					fine_log(ERROR, "BUFFER UNDERRUN playing %zu frames", towrite);
					break;
				case -EBADFD:
					fine_log(ERROR, "output PCM not in the right state");
					break;

				case -ESTRPIPE:
					fine_log(ERROR, "output: A suspend event occured");
					break;
			}
			snd_pcm_prepare(pcm_out); 
		}
		if(written < towrite)
			fine_log(WARN, "expected to write %zu frames, actually wrote %zu frames", towrite, written);
		if(do_fade) {
			gain -= 0.125;
			for(int i = 0; i < written; ++i) {

			}
		}
		left -= written;
	}

	fine_log(DEBUG, "played %zu frames from buffer", sz);
	return 0;
}
/* 
 * The size of the array is always OPT_NUM_RECORDINGS
 * */
void gen_indices(size_t *arr, size_t num_recordings) {
	for(size_t i = 0; i < OPT_NUM_RECORDINGS; ++i) {
		arr[i] = P99_MINOF(num_recordings-1, i);
	}
}

/* 
 * The size of the array is always OPT_NUM_RECORDINGS
 * */
void gen_samples(size_t *arr, size_t max_num) {
	
	for(size_t i = 0; i < OPT_NUM_RECORDINGS; ++i) {
		arr[i] = max_num;
	}

}


int fine_thread_output(void *ptr) {
	int const SAMPLES_PER_FRAME=1;
	ASys *const sys = ptr;
	snd_pcm_prepare(sys->pcm_out);
	snd_pcm_uframes_t period_sz = 0;
	if(snd_pcm_hw_params_get_period_size(sys->hw_in, &period_sz, 0)<0)
		fine_exit("Could not get input period size");
	size_t const num_in_samples = period_sz*SAMPLES_PER_FRAME;

	//DEBUG LOOP FOR NOW: LOCKS UNTIL OUTPUT IS FINISHED
	while(1) {
	
		mtx_lock(&sys->mtx);

		cnd_wait(&sys->playback, &sys->mtx);
		size_t tmpind = (sys->rec_idx-1)%sys->params.max_rec_num;
		fine_output_read_buf(sys->rec_arr[tmpind].data, sys->rec_arr[tmpind].sz, sys->pcm_out, sys->hw_out);
		mtx_unlock(&sys->mtx);
	}
	return 0;

	//NOTE: IN DEV

	size_t recordings_indices[OPT_NUM_RECORDINGS] = {0};
	size_t num_samples[OPT_NUM_RECORDINGS] = {0};
	i16 *const output = 0;
	while(!atomic_load_explicit(&sys->stopped, memory_order_acquire)) {
		gen_indices(recordings_indices, sys->rec_csz);
		gen_samples(num_samples, sys->params.max_num_recording_smp);
		mtx_lock(&sys->mtx);
		cnd_wait(&sys->playback, &sys->mtx);
		int64_t data_sz = 0; //int for now -- prevent wrap around
		for(int i = 0; i < OPT_NUM_RECORDINGS; ++i) { 
			data_sz += num_samples[i]; 
		}
		i16 *data = malloc(sizeof(i16) * data_sz);
		//concat_recordings(data, sys->rec_arr, recordings_indices, num_samples);

		mtx_unlock(&sys->mtx);

		// do_effects(data, data_sz);
		
		 fine_output_read_until(data, data_sz, sys->pcm_out, sys->hw_out, &sys->fade_out); 
	}
	//TODO: cleanup
}
