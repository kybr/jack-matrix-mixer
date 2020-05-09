/**
 * Cross-Fading Matrix Mixer
 * Karl Yerkes, 2020-05-08
 */

// TODO:
//
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
// - adapt to allow CLI option for N inputs/outputs
// - remove globals
// - make a compiling, running jack client app
// - make a Github repository
// - choose a license
// - start a README

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <jack/jack.h>

jack_client_t *client;  // maybe we can have just this one global

typedef struct {
  jack_port_t **in;
  jack_port_t **out;
  jack_port_t **mix;
  jack_default_audio_sample_t **i;
  jack_default_audio_sample_t **o;
  jack_default_audio_sample_t **m;
  bool *connection;
  int size;
} State;

int process(jack_nframes_t N, void *_) {
  State *state = (State *)_;

  for (int k = 0; k < state->size; ++k) {
    state->i[k] =
        (jack_default_audio_sample_t *)jack_port_get_buffer(state->in[k], N);
    state->o[k] =
        (jack_default_audio_sample_t *)jack_port_get_buffer(state->out[k], N);
    state->m[k] =
        (jack_default_audio_sample_t *)jack_port_get_buffer(state->mix[k], N);
  }

  for (int k = 0; k < state->size; ++k) {
    jack_default_audio_sample_t *output = state->o[k];
    jack_default_audio_sample_t *input = state->i[k];

    // wrong; placeholder. copy input to output for testing
    for (int n = 0; n < N; ++n) {
      output[n] = input[n];
    }
  }

  for (int k = 0; k < state->size; ++k) {
    jack_default_audio_sample_t *mix = state->m[k];
    jack_default_audio_sample_t *input = state->i[k];

    // wrong; placeholder. copy input to output for testing
    for (int n = 0; n < N; ++n) {
      mix[n] = input[n];
    }
  }

  return 0;
}

void jack_shutdown(void *arg) { exit(1); }

static void signal_handler(int sig) {
  jack_client_close(client);
  fprintf(stderr, "signal received, exiting ...\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  const char *client_name = "matrix";
  //
  //
  State state;
  jack_status_t status;
  client = jack_client_open(client_name, JackNullOption, &status, NULL);
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

  // initialize state
  //
  state.size = 2;
  if (argc > 1) {
    int h = atoi(argv[1]);
    if (h > 0 && h < 33)  //
      state.size = h;
  }
  state.in = (jack_port_t **)calloc(sizeof(jack_port_t *), state.size);
  state.out = (jack_port_t **)calloc(sizeof(jack_port_t *), state.size);
  state.mix = (jack_port_t **)calloc(sizeof(jack_port_t *), state.size);
  state.i = (jack_default_audio_sample_t **)calloc(
      sizeof(jack_default_audio_sample_t *), state.size);
  state.o = (jack_default_audio_sample_t **)calloc(
      sizeof(jack_default_audio_sample_t *), state.size);
  state.m = (jack_default_audio_sample_t **)calloc(
      sizeof(jack_default_audio_sample_t *), state.size);
  state.connection = (bool *)calloc(sizeof(bool), state.size * state.size);

  // start callback (?)
  //
  jack_set_process_callback(client, process, &state);
  jack_on_shutdown(client, jack_shutdown, 0);

  // register ports
  //
  char port_name[256] = {0};
  for (int k = 0; k < state.size; ++k) {
    // inputs
    //
    sprintf(port_name, "input%d", k);
    state.in[k] = jack_port_register(client, port_name, JACK_DEFAULT_AUDIO_TYPE,
                                     JackPortIsInput, 0);
    if (state.in[k] == NULL) {
      fprintf(stderr, "no more JACK ports available\n");
      exit(1);
    }

    // outputs
    //
    sprintf(port_name, "output%d", k);
    state.out[k] = jack_port_register(
        client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (state.out[k] == NULL) {
      fprintf(stderr, "no more JACK ports available\n");
      exit(1);
    }

    // mixes (also outputs)
    //
    sprintf(port_name, "mix%d", k);
    state.mix[k] = jack_port_register(
        client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (state.mix[k] == NULL) {
      fprintf(stderr, "no more JACK ports available\n");
      exit(1);
    }
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
