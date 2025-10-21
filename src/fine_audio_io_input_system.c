#include "fine_definitions.h" 
#include "fine_log.h"
#include "fine_audio_io.h"
#include "fine_fx_reverb.h"
#include "p99/p99.h"
#include <alsa/asoundlib.h>
#include <limits.h>
#include <threads.h>
#include <string.h>
#include <time.h>


fine_reverb_model my_reverb;

void fine_thread_init_everything(ASys *const res, snd_pcm_hw_params_t *const hw_out, snd_pcm_hw_params_t *const hw_in, snd_pcm_t *const pcm_out, snd_pcm_t *const pcm_in) {
	fine_log(INFO, "Recordings will take around %zu MB of RAM", sizeof(Recording) * MAX_NUM_REC/1000000);
	if(sizeof(Recording) * MAX_NUM_REC/1000000 >= 256) fine_log(WARN, "Recordings using too much memory");

	int toset = 0;
	ASys sys = {
		.idle_buf_idx = 0,
		.idle_buf = calloc(IDLE_BUFSZ, sizeof(i16)),
		.rec_csz=0,
		.rec_idx=0,
		.play = 0,
		.rec_arr=calloc(MAX_NUM_REC, sizeof(Recording)),
		.fade_out=0,
		.hw_out = hw_out,
		.hw_in=hw_in,
		.pcm_out=pcm_out,
		.pcm_in=pcm_in,
		.stopped=0
	};

	memcpy(res, &sys, sizeof(ASys));
	//NOTE: sys is dead now, do not reference it
	
	//set mtx, playback, updaterecordings, nextrecaddr
	mtx_init(&(res->playback_mtx), mtx_plain);
	cnd_init(&(res->playback));

	mtx_init(&(res->fread_mtx), mtx_plain);

	cnd_init(&(res->fread));

	reverb_init(&my_reverb);
	//PRELOAD MY RECORDINGS HERE:
	
	//16 LE
	FILE *curfile;
	size_t file_num = 0;
	while(file_num < MAX_NUM_REC){
		char fname[32] = {0};
		sprintf(fname, "data/%zu.raw", file_num);
		curfile = fopen(fname, "rb");
		if(!curfile) break;

		fseek(curfile, 0, SEEK_END);
		long const num_samples = ftell(curfile)/sizeof(i16);
		if(ftell(curfile)%sizeof(i16))
			fine_log(WARN, "Flie %zu is not divisible into 16 bit segments. Ignoring tail.", file_num);

		rewind(curfile);

		
		res->rec_arr[res->rec_idx].sz = fread(
			res->rec_arr[res->rec_idx].data, sizeof(i16), P99_MINOF(num_samples, RECORDING_SIZE), curfile
		);
		fclose(curfile);


		++file_num;
		res->rec_idx = file_num;
		res->rec_csz = file_num;
	}

	fine_log(INFO, "loaded %zu files into memory", file_num);
}

size_t fine_input_write_until(i16 * const data, size_t const sz, snd_pcm_t *const pcm_in, snd_pcm_hw_params_t *const hw_in, float const alpha, i16 const THRESH_LOWER) {
	
	snd_pcm_sframes_t left = sz;
	snd_pcm_uframes_t tmp_uframe = 0; 
	if(snd_pcm_hw_params_get_period_size(hw_in, &tmp_uframe, 0)<0) 
		fine_exit("Input period size cannot be read");
	snd_pcm_uframes_t const per_read = tmp_uframe;

	assert(per_read < INT_MAX/INT16_MAX);

	float ema = INT16_MAX; //Since we stop on lower threshold
	while(left > 0) {

		fine_log(DEBUG, "ema lower: %f", ema);
		snd_pcm_sframes_t const toread = left < per_read? left : per_read;
		snd_pcm_sframes_t wasread; 
		while((wasread = snd_pcm_readi(pcm_in, data+(sz-left), toread)) < 0) {
			switch(wasread) {
				case -EPIPE:
					fine_log(ERROR, "BUFFER OVERRUN recording %zu frames", toread);
					break;
				case -EBADFD:
					fine_log(ERROR, "output PCM not in the right state");
					break;

				case -ESTRPIPE:
					fine_log(ERROR, "output: A suspend event occured");
					break;
				default:
					//fine_log(ERROR, "Error %d", wasread);
					break;
			}
			snd_pcm_prepare(pcm_in); 
		}
		if(!wasread) continue;
		if(wasread < toread)
			fine_log(WARN, "expected to read %zu frames, actually read %zu frames", toread, wasread);

		int sum = 0;
		for(size_t i = 0; i < wasread; ++i) {
			sum+=abs(data[sz-left+i]);
		}

		ema = alpha * ((float)sum / wasread) + ema * (1-alpha);

		if(ema < THRESH_LOWER) {
			fine_log(DEBUG, "RETURNED EARLY!: wrote %zu samples", sz-left);
			return sz-left;
		}

		left -= wasread;
	}

	fine_log(DEBUG, "wrote %zu samples to without returning early", sz);

	return sz;
}
//TODO: xrun fix
int fine_thread_input_idle(void *ptr) {
	ASys *const sys = ptr;
	snd_pcm_prepare(sys->pcm_in);
	snd_pcm_uframes_t period_sz = 0;
	if(snd_pcm_hw_params_get_period_size(sys->hw_in, &period_sz, 0)<0)
		fine_exit("Could not get input period size");
	size_t const num_in_samples = period_sz; //NOTE: Here one frame is one sample, b/c single channel

	size_t const bufsz = IDLE_BUFSZ;
	assert(num_in_samples <= bufsz);
	
	/* --- BEGIN DEFINITIONS FOR TUNING --- */
	float const alpha_upper = 0.7; 
	float const alpha_lower = 0.5; //high smoothing
	int const THRESH_UPPER = 15000; //is it faster to compare int?
	int const THRESH_LOWER = 1000;
	/* --- END DEFINITIONS FOR TUNING --- */

	i16 *tmp_buf = calloc(num_in_samples, sizeof(*tmp_buf));

	float ema_upper = 0;
	// --- WARM-UP READ ---
	// Read and discard the first buffer
	fine_input_write_buf(tmp_buf, num_in_samples, sys->pcm_in, sys->hw_in);
	// --- END WARM-UP ---	
	bool recording = 0;
	struct timespec last_recording = {0};
	clock_gettime(CLOCK_MONOTONIC_COARSE, &last_recording);
	while(!atomic_load_explicit(&sys->stopped, memory_order_acquire)) {

		fine_log(DEBUG, "ema upper: %f", ema_upper);
		size_t const bufidx = sys->idle_buf_idx;
		sys->idle_buf_idx = (sys->idle_buf_idx + num_in_samples)%bufsz;
		i16 *const idle_buf = sys->idle_buf;

		
		fine_input_write_buf(tmp_buf, num_in_samples, sys->pcm_in, sys->hw_in);

		int sum = 0;
		assert(num_in_samples < INT_MAX/INT16_MAX);
		for(size_t i = 0; i < num_in_samples; ++i) {
			sum += abs(tmp_buf[i]);
			idle_buf[(bufidx + i)%bufsz] = tmp_buf[i]; //should be compiled to a memcpy
		}

                ema_upper = alpha_upper * ((float)sum / num_in_samples) + ema_upper * (1-alpha_upper);
		// fine_log(DEBUG,"EMA: %f", ema_upper);

		struct timespec now = {0};

		clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

		int const seconds_since_approx = now.tv_sec-last_recording.tv_sec;

		if(seconds_since_approx > IDLE_BUFSZ/SAMPLE_RATE) {
			atomic_store_explicit(&sys->play, 0, memory_order_release); //1 seconds chance to play after recording
		}
		
		if(ema_upper >= THRESH_UPPER && seconds_since_approx > IDLE_BUFSZ/SAMPLE_RATE) { //record until lower thresh is reached
			
			atomic_store_explicit(&sys->play, 0, memory_order_release);
			//signal output thread to fade out.
			atomic_store_explicit(&sys->fade_out, 1, memory_order_release);
			//Why not lock here? we make a rule where nobody can access the very last place recording.
			//This is for much better performance
			for(size_t i = 0; i < IDLE_BUFSZ; ++i) {
				sys->rec_arr[sys->rec_idx].data[i] = sys->idle_buf[(sys->idle_buf_idx+i)%IDLE_BUFSZ];
			}
			sys->rec_arr[sys->rec_idx].sz = IDLE_BUFSZ + fine_input_write_until(
				sys->rec_arr[sys->rec_idx].data+IDLE_BUFSZ,
				RECORDING_SIZE-IDLE_BUFSZ,
				sys->pcm_in, sys->hw_in, alpha_lower, THRESH_LOWER
			);

			snd_pcm_drop(sys->pcm_in);

			mtx_lock(&sys->playback_mtx); //There is always work to do at this point

			sys->rec_idx = (sys->rec_idx + 1) % MAX_NUM_REC;
			sys->rec_csz = P99_MINOF(sys->rec_csz+1, MAX_NUM_REC);


			mtx_unlock(&sys->playback_mtx);

			atomic_store_explicit(&sys->play, 1, memory_order_release);
			//Is it posisble that output misses the fade out? Yes, but it's no big deal.
			atomic_store_explicit(&sys->fade_out, 0, memory_order_release);
			//Output can miss this. However, play is active at this point.
			cnd_signal(&sys->playback);

			clock_gettime(CLOCK_MONOTONIC_COARSE, &last_recording);


			snd_pcm_prepare(sys->pcm_in);
		}
        }
	free(tmp_buf);
}
