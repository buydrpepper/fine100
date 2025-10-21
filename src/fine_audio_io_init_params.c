#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>

#include "fine_definitions.h"
#include "fine_log.h"
#include "fine_audio_io.h"



void fine_init_devices(char const*const out_name, char const*const in_name, 
	 register snd_pcm_t **const pcm_out_addr, snd_pcm_t register **const pcm_in_addr,
	 register snd_pcm_hw_params_t *const params_out, register snd_pcm_hw_params_t *const params_in) {

	size_t RATE_IN = 48000;
	size_t RATE_OUT = 48000;
	size_t PERIODS_IN  = 2;
	size_t PERIODS_OUT  = 2;
	size_t PERIOD_SIZE_IN = 8192;
	size_t PERIOD_SIZE_OUT = 8192;
	int ACCESS_IN = SND_PCM_ACCESS_RW_INTERLEAVED;
	int ACCESS_OUT = SND_PCM_ACCESS_RW_INTERLEAVED;
	int FORMAT_IN = SND_PCM_FORMAT_S16_LE;
	int FORMAT_OUT = SND_PCM_FORMAT_S16_LE;


	if(!(out_name && in_name && params_out && params_in && pcm_out_addr && pcm_in_addr)) 
		fine_exit("Fine_init: One of the params are null");

	//Open devices
	if(snd_pcm_open(pcm_out_addr, out_name, SND_PCM_STREAM_PLAYBACK, 0) < 0)
		fine_exit("PCM device %s could not be opened for playback", out_name);
	if(snd_pcm_open(pcm_in_addr, in_name, SND_PCM_STREAM_CAPTURE, 0) < 0)
		fine_exit("PCM device %s could not be opened for capture", in_name);
	fine_log(DEBUG, "devices opened");

	//register to avoid taking address
	register snd_pcm_t *const pcm_out = *pcm_out_addr; 
	register snd_pcm_t *const pcm_in = *pcm_in_addr;

	//Begin hw config
	if(snd_pcm_hw_params_any(pcm_out, params_out) < 0)
		fine_exit("Output device cannot be configured.");
	if(snd_pcm_hw_params_any(pcm_in, params_in) < 0)
		fine_exit("Input device cannot be configured.");
	fine_log(DEBUG, "starting config");

	if(snd_pcm_hw_params_set_access(pcm_out, params_out, ACCESS_OUT) < 0)
		fine_exit("Error setting access: Output");
	if(snd_pcm_hw_params_set_access(pcm_in, params_in, ACCESS_IN) < 0)
		fine_exit("Error setting access: Input");
	fine_log(DEBUG, "set access success");

	if(snd_pcm_hw_params_set_format(pcm_out, params_out, FORMAT_OUT) < 0)
		fine_exit("Error setting format: Output");
	if(snd_pcm_hw_params_set_format(pcm_in, params_in, FORMAT_IN) < 0)
		fine_exit("Error setting format: Input");
	fine_log(DEBUG, "set format success");

	unsigned exact_rate_out = RATE_OUT;
	unsigned exact_rate_in = RATE_IN;
	if(snd_pcm_hw_params_set_rate_near(pcm_out, params_out, &exact_rate_out, 0)<0 )
		fine_exit("Error setting sample rate: Output");
	if(snd_pcm_hw_params_set_rate_near(pcm_in, params_in, &exact_rate_in, 0)<0 )
		fine_exit("Error setting sample rate: Input");

	if(exact_rate_out != RATE_OUT)
		fine_log(WARN, "Output: Rate of %u unsupported, using %u", RATE_OUT, exact_rate_out);
	if(exact_rate_in != RATE_IN)
		fine_log(WARN, "Input: Rate of %u unsupported, using %u", RATE_IN, exact_rate_in);
	fine_log(DEBUG, "set rate success");

	if(snd_pcm_hw_params_set_channels(pcm_out, params_out, 1) < 0)
		fine_exit("Could not set channels to 1: Output");
	if(snd_pcm_hw_params_set_channels(pcm_in, params_in, 1) < 0)
		fine_exit("Could not set channels to 1: Input");
	fine_log(DEBUG, "set channels success");

	if(snd_pcm_hw_params_set_periods(pcm_out, params_out, PERIODS_OUT, 0) <0)
		fine_exit("Could not set periods: Output");
	if(snd_pcm_hw_params_set_periods(pcm_in, params_in, PERIODS_IN, 0) <0)
		fine_exit("Could not set periods: Input");
	fine_log(DEBUG, "set periods success");

	//may fail, use near
	snd_pcm_uframes_t const buf_sz_out = PERIODS_OUT*PERIOD_SIZE_OUT; 
	snd_pcm_uframes_t const buf_sz_in = PERIODS_IN*PERIOD_SIZE_IN; 
	if(snd_pcm_hw_params_set_buffer_size(pcm_out, params_out, buf_sz_out ) < 0)
		fine_exit("Could not set buffer size to %d: Output", buf_sz_out);

	if(snd_pcm_hw_params_set_buffer_size(pcm_in, params_in, buf_sz_in ) < 0)
		fine_exit("Could not set buffer size to %d: Input", buf_sz_in);

	fine_log(DEBUG, "set buffers success");
	if (snd_pcm_hw_params(pcm_out, params_out) < 0)
		fine_exit("Failed to apply hardware parameters: Output");
	if (snd_pcm_hw_params(pcm_in, params_in) < 0)
		fine_exit("Failed to apply hardware parameters: Input");
	fine_log(INFO, "devices are ready");
}

