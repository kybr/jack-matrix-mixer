/**
 * Cross-Fading Matrix Mixer
 * Karl Yerkes, 2020-05-08
 */

// TODO:
//
// - adapt to allow CLI option for N inputs/outputs
// - implement matrix mixing
// - implement cross-fading
// - integrate OSC library
// - copy OSC protocol from the Max patch
// - implement OSC control
// - add automatic connection to JackTrip ports
// - support several common topology
//   + ring, star, trios, full
//
// DONE:
//
// - make a compiling, running jack client app
// - make a Github repository
// - choose a license
// - start a README

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <jack/jack.h>

static jack_port_t in_port[2], out_port[2];
jack_client_t *client;

float gain = 0.5;

int process(jack_nframes_t nframes, void *_) {
#define SAMPLE jack_default_audio_sample_t
  SAMPLE *out0 = (SAMPLE *)jack_port_get_buffer(out_port[0], nframes);
  SAMPLE *out1 = (SAMPLE *)jack_port_get_buffer(out_port[1], nframes);
  SAMPLE *in0 = (SAMPLE *)jack_port_get_buffer(in_port[0], nframes);
  SAMPLE *in1 = (SAMPLE *)jack_port_get_buffer(in_port[1], nframes);
#undef SAMPLE

  // copy the 2 inputs to the 2 outputs, but swap/cross.
  // also attenuate with a global gain
  //
  for (int i = 0; i < nframes; i++) {
    out0[i] = in1[i] * gain;
    out1[i] = in0[i] * gain;
  }

  // implement matrix mixing here
  //
  //

  return 0;
}

void jack_shutdown(void *arg) { exit(1); }

static void signal_handler(int sig) {
  jack_client_close(client);
  fprintf(stderr, "signal received, exiting ...\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  //
  //
  jack_status_t status;
  client = jack_client_open("matrix", JackNullOption, &status, NULL);
  if (client == NULL) {
    fprintf(stderr,
            "jack_client_open() failed, "
            "status = 0x%2.0x\n",
            status);
    if (status & JackServerFailed) {
      fprintf(stderr, "Unable to connect to JACK server\n");
    }
    exit(1);
  }
  if (status & JackServerStarted) {
    fprintf(stderr, "JACK server started\n");
  }
  if (status & JackNameNotUnique) {
    client_name = jack_get_client_name(client);
    fprintf(stderr, "unique name `%s' assigned\n", client_name);
  }

  jack_set_process_callback(client, process, NULL);
  jack_on_shutdown(client, jack_shutdown, 0);

  out_port[0] = jack_port_register(client, "output1", JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsOutput, 0);

  out_port[1] = jack_port_register(client, "output2", JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsOutput, 0);

  in_port[0] = jack_port_register(client, "input1", JACK_DEFAULT_AUDIO_TYPE,
                                  JackPortIsInput, 0);

  in_port[1] = jack_port_register(client, "input2", JACK_DEFAULT_AUDIO_TYPE,
                                  JackPortIsInput, 0);

  if ((out_port[0] == NULL) || (out_port[1] == NULL) || (in_port[0] == NULL) ||
      (in_port[1] == NULL)) {
    fprintf(stderr, "no more JACK ports available\n");
    exit(1);
  }

  // GO!
  //
  if (jack_activate(client)) {
    fprintf(stderr, "cannot activate client");
    exit(1);
  }

  // script automatic connections here?
  //

#ifdef WIN32
  signal(SIGINT, signal_handler);
  signal(SIGABRT, signal_handler);
  signal(SIGTERM, signal_handler);
#else
  signal(SIGQUIT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);
#endif

  for (;;) {
#ifdef WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
  }

  jack_client_close(client);
  exit(0);
}
