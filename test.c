/**
 * Make N channels of sine waves
 * Karl Yerkes, 2020-05-08
 */

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

jack_client_t *client;  // maybe we can have just this one global

#define TABLE_SIZE (16384)
#define SAMPLE_RATE (44100)
jack_default_audio_sample_t sine[TABLE_SIZE];
typedef struct {
  jack_port_t **out;
  jack_default_audio_sample_t **o;
  jack_default_audio_sample_t *phase;
  int out_count;
} State;

int process(jack_nframes_t N, void *_) {
  State *state = (State *)_;

  for (int k = 0; k < state->out_count; ++k)
    state->o[k] =
        (jack_default_audio_sample_t *)jack_port_get_buffer(state->out[k], N);

  for (int k = 0; k < state->out_count; ++k) {
    jack_default_audio_sample_t *output = state->o[k];
    for (int j = 0; j < N; j++) {
      output[j] = sine[(int)(state->phase[k] * TABLE_SIZE)];
      state->phase[k] += 110.0f * (1 + k) / SAMPLE_RATE;
      if (state->phase[k] > 1)  //
        state->phase[k] -= 1;
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
  const char *client_name = "test";
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

  for (int k = 0; k < TABLE_SIZE; ++k)  //
    sine[k] = sin(2 * M_PI * k / TABLE_SIZE);

  state.out_count = 2;
  if (argc > 1) {
    int h = atoi(argv[1]);
    if (h > 0 && h < 256)  //
      state.out_count = h;
  }
  state.out = (jack_port_t **)calloc(sizeof(jack_port_t *), state.out_count);
  state.o = (jack_default_audio_sample_t **)calloc(
      sizeof(jack_default_audio_sample_t *), state.out_count);
  state.phase = (jack_default_audio_sample_t *)calloc(
      sizeof(jack_default_audio_sample_t), state.out_count);

  jack_set_process_callback(client, process, &state);
  jack_on_shutdown(client, jack_shutdown, 0);

  for (int k = 0; k < state.out_count; ++k) {
    char output_name[256] = {0};
    sprintf(output_name, "output%d", k);
    state.out[k] = jack_port_register(
        client, output_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (state.out[k] == NULL) {
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
