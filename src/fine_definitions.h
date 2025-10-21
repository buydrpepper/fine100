#pragma once
#include <stdint.h>
#include <alsa/asoundlib.h>
#include <threads.h>
#include <stdbool.h>

typedef int16_t i16;

typedef struct ASys ASys;
typedef struct ASys_params ASys_params;
typedef struct Recording Recording;
#define SAMPLE_RATE 48000
#define RECORDING_SIZE (SAMPLE_RATE*4) //Max recording length is 4 seconds
#define IDLE_BUFSZ SAMPLE_RATE
#define MAX_NUM_REC 512
#define OPT_NUM_RECORDINGS 10
struct Recording {
	size_t sz;
	i16 data[RECORDING_SIZE];
};
struct ASys {

	/*only the input thread will access idx, csz, next_rec_addr, and dereference idle_buf*/
	size_t idle_buf_idx;
	size_t idle_buf_csz;
	i16 *const idle_buf;

	mtx_t fread_mtx;
	cnd_t fread;

	mtx_t playback_mtx;
	cnd_t playback;
	_Atomic(bool) play; //set true /false by input thread, read by output thread.

	_Atomic(bool) fade_out; 
	/* Protected by mtx-- Output thread will unlock until signaled playback
	 * Input thread captures mtx when playback ends AND input finishes. Then, output captures mtx to compute effects and playback
	 * */
	size_t rec_csz;
	size_t rec_idx;
	//NOTE: the very last recording(the max_rec_num place, if it were a queue) is not to be read
	//and only to be written by the input thread
	Recording *const rec_arr; //Each recording has the max possible size. Make sure this fits into 256MB

	snd_pcm_hw_params_t *const hw_out;
	snd_pcm_hw_params_t *const hw_in;
	snd_pcm_t *const pcm_out;
	snd_pcm_t *const pcm_in;

	_Atomic(bool) stopped;
};



