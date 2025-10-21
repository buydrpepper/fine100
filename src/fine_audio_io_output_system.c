
#include "fine_definitions.h"
#include "fine_log.h"
#include "fine_audio_io.h"
#include "fine_fx.h"
#include "fine_fx_reverb.h"
#include "p99/p99.h"
#include <alsa/asoundlib.h>
#include <limits.h>
#include <threads.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>


extern fine_reverb_model my_reverb;

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
				data[sz-left+i]*=gain;
			}
		}
		left -= written;
	}

	fine_log(DEBUG, "played %zu frames from buffer", sz);
	return 0;
}


int frand() {
	return rand();
}

/* 
 * The size of the array is always OPT_NUM_RECORDINGS
 * 0 means the earliest recording, 1 means the second earliest, etc.
 * */
void gen_indices(size_t *const arr, size_t const num_recordings) {
	//NOTE: IMPORTANT! do NOT access the very back recording (never set an index to MAX_NUM_REC-1. it is not thread safe

	size_t idx = 0;
	arr[idx] = 0;
	++idx;

	for(; idx<num_recordings; ++idx) {
		arr[idx] = idx;
	}
}

/* 
 * The size of the array is always OPT_NUM_RECORDINGS
 * */
void gen_samples(size_t *const arr, size_t const max_num) {
	
	for(size_t i = 0; i < OPT_NUM_RECORDINGS; ++i) {
		arr[i] = max_num;
	}

}

/*
 * @return the size of the rendered sound, guaranteed to be the sum of num_tail_samples and the array num_samples
 * */
int render_recordings(i16 *const data, ASys const*const sys, size_t const*const indices, size_t const*const num_samples, size_t const num_tail_samples) {
	Recording const*const recs = sys->rec_arr;
	size_t const start_idx = sys->rec_idx-1;
	size_t ind_towrite = 0;
	//simply concat for now
	for(size_t i =0 ; i < OPT_NUM_RECORDINGS; ++i) {
		int tmp_rec[] = {0};
		

		memcpy(data+ind_towrite, recs[(start_idx-indices[i])%MAX_NUM_REC].data, sizeof(i16)*num_samples[i]);
		float ampfac = frand();

		fine_fx_amplify(data+ind_towrite, num_samples[i], 4*(float)ampfac/RAND_MAX);
		fine_fx_compress(data+ind_towrite, num_samples[i], 48000, 5000.0f, 10.0f, 3.0f, 80.0f, 1.0f);

		fine_fx_fade_linear(data+ind_towrite, num_samples[i], 48000, 48000);

//   fine_fx_compress(samples, n_samples, sample_rate,
//                    8000.0f,   // threshold (linear, same scale as int16 samples, e.g. 32767 max)
//                    4.0f,      // ratio (>=1.0)
//                    5.0f,      // attack_ms
//                    80.0f,     // release_ms
//                    1.0f);     // makeup gain (linear multiplier)

		ind_towrite += num_samples[i];
	}

	size_t const total_num_samples = ind_towrite + num_tail_samples;

	float r1 = (float)frand()/RAND_MAX;
	float r2 = (float)frand()/RAND_MAX;
	float room = r1;
	float damp = r2;
	float wet  = r1;
	float dry  = 1-r1;
	reverb_set_params(&my_reverb, room, damp, wet, dry);
	fine_fx_reverb(data, total_num_samples, &my_reverb);
	return total_num_samples;
}


int fine_thread_output(void *ptr) {
	ASys *const sys = ptr;
	snd_pcm_drop(sys->pcm_out);

	unsigned CUR_RATE = 0;
	if(snd_pcm_hw_params_get_rate(sys->hw_in, &CUR_RATE,0)<0)
		fine_exit("Could not get sample rate");

	//NOTE: IN DEV

	size_t recordings_indices[OPT_NUM_RECORDINGS] = {0};
	size_t num_samples[OPT_NUM_RECORDINGS] = {0};

	size_t const NUM_TAIL_SAMPLES = CUR_RATE*2; //2 seconds
	//NOTE: IMPORTANT: output length must be the same as the recordings concatentaed, plus the tail
	i16 *data = calloc(NUM_TAIL_SAMPLES + RECORDING_SIZE*OPT_NUM_RECORDINGS, sizeof(i16));
	while(!atomic_load_explicit(&sys->stopped, memory_order_acquire)) {
		mtx_lock(&sys->playback_mtx);
		while(!atomic_load_explicit(&sys->play, memory_order_acquire)) {
			cnd_wait(&sys->playback, &sys->playback_mtx);
		}
		//This needs to be fast
		gen_indices(recordings_indices, sys->rec_csz);
		gen_samples(num_samples, RECORDING_SIZE);
		
		mtx_unlock(&sys->playback_mtx);

		//NOTE: We don't lock bc we won't read from the very last recording (the one that the input thread is actually touching)
		int64_t data_sz = render_recordings(data, sys, recordings_indices, num_samples, NUM_TAIL_SAMPLES);


		fine_log(DEBUG, "expecting to play %zu seconds", data_sz/CUR_RATE);
		
		snd_pcm_prepare(sys->pcm_out);
		//Blocks until playback ends
		fine_output_read_until(data, data_sz, sys->pcm_out, sys->hw_out, &sys->fade_out); 

		snd_pcm_drop(sys->pcm_out);
	}
	free(data);
	//TODO: cleanup

}
