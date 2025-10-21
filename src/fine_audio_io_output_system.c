
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

int fine_output_read_until(i16 const*const data, size_t const sz, snd_pcm_t *const pcm_out, snd_pcm_hw_params_t *const hw_out, _Atomic(bool) *fade_out) {
	//TODO: check if writing FULL buffer works
	
	snd_pcm_sframes_t left = sz;
	snd_pcm_uframes_t tmp_uframe = 0; 
	if(snd_pcm_hw_params_get_period_size(hw_out, &tmp_uframe, 0)<0) 
		fine_exit("Input period size cannot be read");
	snd_pcm_uframes_t const per_write = tmp_uframe;
	i16 *write_buf = calloc(per_write, sizeof(i16));
	float gain = 1;
	int do_fade = 0;
	while(left > 0 && gain > 0.01) {
		if(!do_fade && atomic_load_explicit(fade_out, memory_order_acquire)) {
			do_fade = 1; 
			atomic_store_explicit(fade_out, 0, memory_order_release);
		}
		snd_pcm_sframes_t const towrite = left < per_write? left : per_write;
		snd_pcm_sframes_t written; 
		memcpy(write_buf, data+(sz-left), towrite*sizeof(i16));

		if(do_fade) {
			gain -= (float)1/16;
			for(size_t i = 0; i < towrite; ++i) {
				write_buf[i] = roundf(write_buf[i]*gain);
			}
		}
		while((written = snd_pcm_writei(pcm_out, write_buf, towrite)) < 0) {
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
		left -= written;
	}
	free(write_buf);

	fine_log(DEBUG, "played %zu frames from buffer", sz);
	return 0;
}


//TODO: implement
int fast_rand() {
	return rand();
}

typedef struct TimeFrame TimeFrame;
struct TimeFrame {
	size_t offs;
	size_t num_samples;
};
/* 
 * 
 * The size of the array is always OPT_NUM_RECORDINGS
 * 0 means the newest recording, 1 means the second newest, etc.
 * @return the number of recordings generated
 * */
size_t gen_indices(size_t *const arr, size_t const num_recordings) {
	//NOTE: IMPORTANT! do NOT access the very back recording (never set an index to MAX_NUM_REC-1. it is not thread safe

	size_t idx = 0;
	for(size_t i = 0; i < OPT_NUM_RECORDINGS; ++i) {
		size_t r = fast_rand()%P99_MINOF(num_recordings, MAX_NUM_REC-1);
		for(size_t j = 0; j < idx; ++j) {
			if (arr[j] == r) goto SKIP;
		}
		arr[idx] = r;
		++idx;
		SKIP:
	}
	return idx;
}

/* 
 * @param arr should be zeroed
 * The size of the array is always OPT_NUM_RECORDINGS
 * */
void gen_samples(
	TimeFrame *const smpl_arr, Recording const*const recordings, size_t const newest_rec_idx,
	size_t const*const indices, size_t const end_idx, size_t const max_num_samples) {

	//TODO: implement max num samples someway
	assert(end_idx <= OPT_NUM_RECORDINGS);
	
	size_t const step = RECORDING_SIZE/(8*3); //allow thirds and eights
	assert(step>0);
	for(size_t i = 0; i < end_idx; ++i) {
		size_t const recsz = recordings[((size_t)MAX_NUM_REC + newest_rec_idx-indices[i])%MAX_NUM_REC].sz;
		if(!recsz ){
			fine_log(WARN, "Warning: Recording of size zero");
			smpl_arr[i] = (TimeFrame){0,0};
			continue;
		}
		int r = fast_rand();
		size_t const requestedsz = step*((r%recsz)/step);
		size_t const finalsz = requestedsz == 0 ? recsz : requestedsz;
		smpl_arr[i].num_samples=finalsz;
		int r2 = fast_rand();
		size_t const reqoffs = step*((r2%(recsz-finalsz+1))/step);
		int r3 = fast_rand();
		smpl_arr[i].offs = r3%3? 0 : reqoffs;

		assert(smpl_arr[i].num_samples+smpl_arr[i].offs <= RECORDING_SIZE);
	}
}

/* Safe if num_samples[i] is 0. In this case it just skips it.
 * @return the size of the rendered sound, guaranteed to be the sum of num_tail_samples and the array num_samples
 * 
 * */
int render_recordings(i16 *const data, size_t data_sz, Recording const*const recordings, size_t const newest_rec_idx, size_t const*const indices, size_t const num_recordings_selected, TimeFrame const*const timeframes, size_t const num_tail_samples) {
	size_t ind_towrite = 0;

	i16 *cur_render = calloc(RECORDING_SIZE+num_tail_samples,sizeof(i16));

	int32_t *mixed = calloc(data_sz, sizeof *mixed);
	//simply concat for now
	for(size_t i =0 ; i < num_recordings_selected; ++i) {

		//prevent reverb feedback. NOTE: if later samples affect earlier ones, this is not enough.
		memset(cur_render+timeframes[i].num_samples, 0, num_tail_samples * sizeof(i16));

		//-1 because index points to the currently working index
		memcpy(cur_render, recordings[((size_t)MAX_NUM_REC + newest_rec_idx-indices[i])%MAX_NUM_REC].data+timeframes[i].offs, sizeof(i16)*timeframes[i].num_samples);
		int r = fast_rand();

		fine_fx_amplify(cur_render, timeframes[i].num_samples, 6.0f + 5*(r%3));
		fine_fx_compress(cur_render, timeframes[i].num_samples, SAMPLE_RATE, 4000.0f, 10.0f, 3.0f, 80.0f, 1.0f);

		//   fine_fx_compress(samples, n_samples, sample_rate,
		//                    8000.0f,   // threshold (linear, same scale as int16 samples, e.g. 32767 max)
		//                    4.0f,      // ratio (>=1.0)
		//                    5.0f,      // attack_ms
		//                    80.0f,     // release_ms
		//                    1.0f);     // makeup gain (linear multiplier)

		size_t const NUM_FADE_SAMPLES = timeframes[i].num_samples/8;
		fine_fx_fade_linear(cur_render, timeframes[i].num_samples, NUM_FADE_SAMPLES, NUM_FADE_SAMPLES);


		float r1 = (float)(fast_rand()%4)/3;
		float r2 = (float)(fast_rand()%3)/2;
		float r3 = (float)(fast_rand()%3)/2;
		float room = r1;
		float damp = r1;//2
		float wet  = r3;
		float dry  = 1-r3;
		

		reverb_set_params(&my_reverb, room, damp, wet, dry);
		reverb_reset(&my_reverb); //must be called to destroy prev. samples
		fine_fx_reverb(cur_render, timeframes[i].num_samples+num_tail_samples, &my_reverb);

		for(size_t j = 0; j < timeframes[i].num_samples+num_tail_samples; ++j) {
			mixed[ind_towrite+j] += cur_render[j];
		}

		//Actually, this can be anything we like as long as it doesn't overflow.
		//The following line is the natural thing to do:
		ind_towrite += timeframes[i].num_samples; 
		//This line is added to "blend" the samples together
		if(i < num_recordings_selected-1) 
			ind_towrite -= (4*(1-r2)+2)*NUM_FADE_SAMPLES;
	}


	free(cur_render);

	size_t const total_num_samples = ind_towrite + num_tail_samples;
	assert(total_num_samples <= data_sz);



	//Limiter to prevent clipping
	//potential off by one error is negligible
	i16 const THRESHOLD = INT16_MAX;
	float curgain = 8.0f;
	float targain = 1.0f;
	float const attack = 0.1f;
	float const rel = 0.99f;
	for(size_t i =0; i< total_num_samples; ++i) {
		float const mag = fabs(curgain*(mixed[i]));
		targain = mag > THRESHOLD? THRESHOLD/mag : 1.0f;
		if(targain < curgain) 
			curgain = targain*(1.0f-attack) + attack*curgain;
		else 
			curgain = targain*(1.0f-rel) + rel*curgain;
		float new = curgain*mixed[i];
		if(new >= INT16_MAX) new = INT16_MAX;
		else if (new <= INT16_MIN) new = INT16_MIN;
		data[i] = roundf(new);
	}
	free(mixed);
	return total_num_samples;
}

int fine_thread_output(void *ptr) {
	ASys *const sys = ptr;
	snd_pcm_drop(sys->pcm_out);

	size_t recordings_indices[OPT_NUM_RECORDINGS] = {0};
	TimeFrame timeframes[OPT_NUM_RECORDINGS] = {0};

	size_t const NUM_TAIL_SAMPLES = SAMPLE_RATE*8; //8 seconds
	//NOTE: IMPORTANT: output length must be the same as the recordings concatentaed, plus the tail
	size_t const DATA_SZ = NUM_TAIL_SAMPLES + RECORDING_SIZE*OPT_NUM_RECORDINGS;
	i16 *data = calloc(DATA_SZ, sizeof(i16));
	while(!atomic_load_explicit(&sys->stopped, memory_order_acquire)) {
		mtx_lock(&sys->playback_mtx);
		while(!atomic_load_explicit(&sys->play, memory_order_acquire)) {
			cnd_wait(&sys->playback, &sys->playback_mtx);
		}
		//This needs to be fast
		size_t end_ind = gen_indices(recordings_indices, sys->rec_csz);

		size_t rec_idx = sys->rec_idx;	
		gen_samples(timeframes, sys->rec_arr, rec_idx-1, recordings_indices, end_ind, RECORDING_SIZE);
		mtx_unlock(&sys->playback_mtx);

		//NOTE: We don't lock bc we won't read from oldest recording (the one that the input thread is actually touching)
		size_t data_sz = render_recordings(data, DATA_SZ, sys->rec_arr, rec_idx-1, recordings_indices, end_ind, timeframes, NUM_TAIL_SAMPLES);

		fine_log(DEBUG, "expecting to play %zu seconds", data_sz/SAMPLE_RATE);
		
		snd_pcm_prepare(sys->pcm_out);
		//Blocks until playback ends
		fine_output_read_until(data, data_sz, sys->pcm_out, sys->hw_out, &sys->fade_out); 
		snd_pcm_drop(sys->pcm_out);
	}
	free(data);

}
