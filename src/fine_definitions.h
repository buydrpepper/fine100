#pragma once
#include <stdint.h>
#include <alsa/asoundlib.h>
#include <threads.h>

typedef int16_t i16;

typedef struct ASys ASys;
typedef struct ASys_params ASys_params;
typedef struct Recording Recording;
struct Recording {
	size_t sz;
	i16 data[];
};
struct ASys_params {
	size_t max_num_recording_smp;
	size_t idle_buf_sz;
	size_t max_rec_num;
};
struct ASys {

	ASys_params const params;

	/*only the input thread will access idx, csz, next_rec_addr, and dereference idle_buf*/
	size_t idle_buf_idx;
	size_t idle_buf_csz;
	i16 *const idle_buf;


	mtx_t mtx;
	cnd_t playback;

	_Atomic(bool) fade_out; //set true by input thread, read by output thread and set false
	cnd_t update_recordings;
	/* Protected by mtx-- Output thread will unlock until signaled playback
	 * Input thread captures mtx when playback ends AND input finishes. Then, output captures mtx to compute effects and playback
	 * */
	size_t rec_csz;
	size_t rec_idx;
	Recording *const rec_arr; //Each recording has the max possible size. Make sure this fits into 256MB

	snd_pcm_hw_params_t *const hw_out;
	snd_pcm_hw_params_t *const hw_in;
	snd_pcm_t *const pcm_out;
	snd_pcm_t *const pcm_in;

	_Atomic(bool) stopped;
};


#define OPT_NUM_RECORDINGS 10

