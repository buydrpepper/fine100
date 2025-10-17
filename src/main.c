#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "fine_definitions.h"
#include "fine_log.h"
#include "fine_audio_io.h"
#include "fine_fx.h"


int debugthread(void *ptr) {

	ASys *const sys = ptr;
	while(1) {
		char c = getchar();
		if(c == 'p') 
			cnd_signal(&sys->playback);
		else if(c == 'q') exit(0);
	}
}




int main(int argc, char *argv[argc+1]) {

	char name_out[32] = {0};
	char name_in[32] = {0};
	if(argc != 3) {
		FILE *const namefile = fopen("names", "r");
		if(!namefile) fine_exit("Expected a newline separated file or arguments: [output name] [input name]");
		if(fgets(name_out, 32, namefile)<0 || !name_out[0])
			fine_exit("read from names file failed");
		if(fgets(name_in, 32, namefile)<0 || !name_in[0])
			fine_exit("read from names file failed");

		name_out[strcspn(name_out, "\n")]=0;
		name_in[strcspn(name_in, "\n")]=0;
		fclose(namefile);
	}
	else {
		memcpy(name_out, argv[1], 31);
		memcpy(name_in, argv[2], 31);
	}

	snd_pcm_t *pcm_out = 0;
	snd_pcm_t *pcm_in = 0;
	snd_pcm_hw_params_t *params_out = 0;
	snd_pcm_hw_params_t *params_in = 0;
	snd_pcm_hw_params_alloca(&params_out);
	snd_pcm_hw_params_alloca(&params_in);

	while(fine_init_devices(name_out, name_in, &pcm_out, &pcm_in, params_out, params_in)<0) {
		struct timespec delay = {.tv_sec=3};
		thrd_sleep(&delay, 0);
		fine_log(INFO, "Configuration failed. Retrying...");
	}

	ASys *const sys = alloca(sizeof(ASys));
	fine_thread_init_everything(sys, params_out, params_in, pcm_out, pcm_in);


	thrd_t thrd[3];
	thrd_create(thrd+0, fine_thread_input_idle, sys);
	thrd_create(thrd+1, fine_thread_output, sys);
	thrd_create(thrd+2, debugthread, sys);

	thrd_join(thrd[0], 0);
	thrd_join(thrd[1], 0);
	thrd_join(thrd[2], 0);


	printf("hi");
	

	




}
