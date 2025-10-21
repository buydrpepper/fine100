#include "fine_definitions.h" 
#include "fine_log.h"
#include "fine_audio_io.h"
#include "p99/p99.h"
#include <alsa/asoundlib.h>
#include <limits.h>
#include <threads.h>
#include <string.h>
#include <time.h>


void fine_thread_init_everything(ASys *const res, snd_pcm_hw_params_t *const hw_out, snd_pcm_hw_params_t *const hw_in, snd_pcm_t *const pcm_out, snd_pcm_t *const pcm_in) {
	unsigned CUR_RATE = 0;
	unsigned const input_buffer_length_ms = 500;
	if(snd_pcm_hw_params_get_rate(hw_in, &CUR_RATE,0)<0)
		fine_exit("Could not get sample rate");
	fine_log(DEBUG, "RATE: %u", CUR_RATE);
	ASys_params params = {
		.idle_buf_sz = (CUR_RATE/1000)*input_buffer_length_ms, 
		.max_num_recording_smp=CUR_RATE*2,
		.max_rec_num=512
	};
	size_t const REC_SZ = P99_FSIZEOF(Recording, data, params.max_num_recording_smp);
	fine_log(INFO, "Recordings will take around %zu MB of RAM", REC_SZ * params.max_rec_num/1000000);
	if(REC_SZ * params.max_rec_num/1000000 >= 256) fine_log(WARN, "Recordings using too much memory");

	int toset = 0;
	ASys sys = {
		.params=params,
		.idle_buf_idx = 0,
		.idle_buf = calloc(params.idle_buf_sz, sizeof(i16)),
		.rec_csz=0,
		.rec_idx=0,
		.rec_arr=calloc(params.max_rec_num, REC_SZ),
		.fade_out=0,
		.hw_out = hw_out,
		.hw_in=hw_in,
		.pcm_out=pcm_out,
		.pcm_in=pcm_in,
		.stopped=0
	};

	memcpy(res, &sys, sizeof(ASys));
	//set mtx, playback, updaterecordings, nextrecaddr
	mtx_init(&(res->mtx), mtx_plain);
	cnd_init(&(res->playback));
	cnd_init(&(res->update_recordings));
}

int fine_input_write_until(i16 * const data, size_t const sz, snd_pcm_t *const pcm_in, snd_pcm_hw_params_t *const hw_in, float const alpha, i16 const THRESH_LOWER) {
	
	snd_pcm_sframes_t left = sz;
	snd_pcm_uframes_t tmp_uframe = 0; 
	if(snd_pcm_hw_params_get_period_size(hw_in, &tmp_uframe, 0)<0) 
		fine_exit("Input period size cannot be read");
	snd_pcm_uframes_t const per_read = tmp_uframe;

	assert(per_read < INT_MAX/INT16_MAX);

	float ema = INT16_MAX; //Since we stop on lower threshold
	while(left > 0) {
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
		if(wasread < toread)
			fine_log(WARN, "expected to read %zu frames, actually read %zu frames", toread, wasread);

		int sum = 0;
		for(size_t i = 0; i < wasread; ++i) {
			sum+=abs(data[sz-left+i]);
		}

		ema = alpha * sum / wasread + ema * (1-alpha);

		if(ema < THRESH_LOWER) {
			fine_log(DEBUG, "RETURNED EARLY!: wrote %zu samples", sz);
			return sz-left;
		}

		left -= wasread;
	}

	fine_log(DEBUG, "wrote %zu samples to without returning early", sz);

	return sz;
}

int fine_thread_input_idle(void *ptr) {
	int const SAMPLES_PER_FRAME=1;
	ASys *const sys = ptr;
	snd_pcm_prepare(sys->pcm_in);
	snd_pcm_uframes_t period_sz = 0;
	if(snd_pcm_hw_params_get_period_size(sys->hw_in, &period_sz, 0)<0)
		fine_exit("Could not get input period size");
	size_t const num_in_samples = period_sz*SAMPLES_PER_FRAME;

	size_t const bufsz = sys->params.idle_buf_sz;
	assert(num_in_samples <= bufsz);
	
	float const alpha_upper = 0.9; //NOTE: For faster perf, use fixed point
	float const alpha_lower = 0.5; //high smoothing
	float ema_upper = 0;
	int const THRESH_UPPER = 0.3*INT16_MAX; //is it faster to compare int?
	int const THRESH_LOWER = 1000;

	i16 *tmp_buf = calloc(num_in_samples, sizeof(*tmp_buf));

	// --- WARM-UP READ ---
	// Read and discard the first buffer
	fine_input_write_buf(tmp_buf, num_in_samples, sys->pcm_in, sys->hw_in);
	// --- END WARM-UP ---	
	bool recording = 0;
	while(!atomic_load_explicit(&sys->stopped, memory_order_acquire)) {
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

                ema_upper = alpha_upper * sum / num_in_samples + ema_upper * (1-alpha_upper);
		fine_log(DEBUG,"EMA: %f", ema_upper);
		if(ema_upper >= THRESH_UPPER) { //record until lower thresh is reached
			//signal output thread to fade out.
			atomic_store_explicit(&sys->fade_out, 1, memory_order_release);

			//TODO: use the idle buf!
			mtx_lock(&sys->mtx); //There is always work to do at this point
			//Locked -> output thread is done copying
			//Why lock? Output thread may access rec_idx
			sys->rec_arr[sys->rec_idx].sz = fine_input_write_until(
				sys->rec_arr[sys->rec_idx].data,
				sys->params.max_num_recording_smp,
				sys->pcm_in, sys->hw_in, alpha_lower, THRESH_LOWER
			);

			snd_pcm_drop(sys->pcm_in);

			
			sys->rec_idx = (sys->rec_idx + 1) % sys->params.max_rec_num;
			sys->rec_csz = P99_MINOF(sys->rec_csz+1, sys->params.max_rec_num);

			mtx_unlock(&sys->mtx);
			cnd_signal(&sys->playback);

			struct timespec delay = {.tv_sec = 1};
			thrd_sleep(&delay, 0);

			snd_pcm_prepare(sys->pcm_in);
		}
        }
	free(tmp_buf);
}
