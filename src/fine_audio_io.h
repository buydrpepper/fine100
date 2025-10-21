#pragma once

#include <alsa/asoundlib.h>
#include "fine_definitions.h"
#include <threads.h>

void fine_init_devices(char const* out_name, char const* in_name, 
		 snd_pcm_t **pcm_out_addr, snd_pcm_t **pcm_in_addr,
		 snd_pcm_hw_params_t *params_out, snd_pcm_hw_params_t *params_in);

int fine_input_write_buf(i16 * data, size_t sz, snd_pcm_t *pcm_in, snd_pcm_hw_params_t *hw_in);
int fine_input_write_until(i16 * data, size_t sz, snd_pcm_t *pcm_in, snd_pcm_hw_params_t *hw_in, float alpha, i16 THRESH_LOWER);
int fine_output_read_buf(i16 * data, size_t sz, snd_pcm_t *pcm_out, snd_pcm_hw_params_t *hw_out);


void fine_thread_init_everything(ASys *res, snd_pcm_hw_params_t *hw_out, snd_pcm_hw_params_t *hw_in, snd_pcm_t *pcm_out, snd_pcm_t *pcm_in);
int fine_thread_input_idle(void *ptr);
int fine_thread_output(void *ptr);

