#include "fine_audio_io.h"
#include "fine_definitions.h"
#include "fine_log.h"
#include <alsa/asoundlib.h>
#include <limits.h>
//NOTE: MAX LENGTH IS ABOUT >= A SECOND
//
//Returns the absolute sum of data points
int fine_input_write_buf(i16 * const data, size_t const sz, snd_pcm_t *const pcm_in, snd_pcm_hw_params_t *const hw_in) {
	
	snd_pcm_sframes_t left = sz;
	snd_pcm_uframes_t tmp_uframe = 0; 
	if(snd_pcm_hw_params_get_period_size(hw_in, &tmp_uframe, 0)<0) 
		fine_exit("Input period size cannot be read");
	snd_pcm_uframes_t const per_read = tmp_uframe;

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
		left -= wasread;
	}

	fine_log(DEBUG, "wrote %zu frames to buffer", sz);

	return 0;
}



int fine_output_read_buf(i16 * const data, size_t const sz, snd_pcm_t *const pcm_out, snd_pcm_hw_params_t *const hw_out) {

	//TODO: check if writing FULL buffer works
	
	snd_pcm_sframes_t left = sz;
	snd_pcm_uframes_t tmp_uframe = 0; 
	if(snd_pcm_hw_params_get_period_size(hw_out, &tmp_uframe, 0)<0) 
		fine_exit("Input period size cannot be read");
	snd_pcm_uframes_t const per_write = tmp_uframe;
	while(left > 0) {
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
		left -= written;
	}

	fine_log(DEBUG, "played %zu frames from buffer", sz);
	return 0;
}



