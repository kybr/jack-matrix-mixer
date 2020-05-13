/**
 * Cross-Fading Matrix Mixer
 * Karl Yerkes, 2020-05-08
 */

// TODO:
//
// - test with JackTrip
// - copy OSC protocol from the Max patch
// - add automatic connection to JackTrip ports
// - support several common topology
//   + ring, star, trios, full
//
// DONE:
//
// - implement cross-fading
// - note where to put locks
// - implement OSC control (of absolute matrix state)
// - integrate OSC library
// - fix clicks at the block rate
//   + you have to clear the output buffer before accumulating
// - implement matrix mixing
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
#include <lo/lo.h>

jack_client_t *client;  // maybe we can have just this one global

typedef struct {
  jack_port_t **in;
  jack_port_t **out;
  jack_port_t **mix;
  jack_default_audio_sample_t **i;
  jack_default_audio_sample_t **o;
  jack_default_audio_sample_t **m;
  float *gain;
  int size;
  bool new;
} State;

void print(float *matrix, int size) {
  printf("~~~~~~~( matrix )~~~~~~~~~~\n");
  for (int k = 0; k < size * size; ++k) {
    if (k % size == 0 && k != 0) printf("\n");
    printf("%f ", matrix[k]);
  }
  printf("\n");
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
}

static float *gain;
static float *target;
static float *increment;

int process(jack_nframes_t N, void *_) {
  State *state = (State *)_;

  // XXX try lock
  if (state->new) {
    state->new = false;
    for (int i = 0; i < state->size * state->size; ++i) {
      target[i] = state->gain[i];
      increment[i] = (target[i] - gain[i]) / (0.1 * 44100);
    }
  }
  // XXX free lock

  for (int k = 0; k < state->size; ++k) {
    state->i[k] =
        (jack_default_audio_sample_t *)jack_port_get_buffer(state->in[k], N);
    state->o[k] =
        (jack_default_audio_sample_t *)jack_port_get_buffer(state->out[k], N);
    state->m[k] =
        (jack_default_audio_sample_t *)jack_port_get_buffer(state->mix[k], N);
  }

  // matrix
  //
  for (int k = 0; k < state->size; ++k) {
    jack_default_audio_sample_t *output = state->o[k];
    for (int n = 0; n < N; ++n) {
      output[n] = 0;
    }

    for (int j = 0; j < state->size; ++j) {
      int index = j * state->size + k;
      float g = gain[index];
      float t = target[index];
      float i = increment[index];

      jack_default_audio_sample_t *input = state->i[j];
      for (int n = 0; n < N; ++n) {
        // linear ramp from value to target
        //
        if (g != t) {
          g += i;
          if (i < 0 ? (g < t) : (g > t))  //
            g = t;
        }

        output[n] += input[n] * g;
      }

      gain[index] = g;
    }
  }

  // mixes: everyone gets a mix of everyone but them
  //
  for (int k = 0; k < state->size; ++k) {
    jack_default_audio_sample_t *mix = state->m[k];
    for (int n = 0; n < N; ++n) {
      mix[n] = 0;
    }

    for (int j = 0; j < state->size; ++j) {
      if (j == k) continue;

      jack_default_audio_sample_t *input = state->i[j];
      for (int n = 0; n < N; ++n) {
        mix[n] += input[n];
      }
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

void error(int num, const char *msg, const char *path) {
  printf("liblo server error %d in path %s: %s\n", num, path, msg);
  fflush(stdout);
}

/* catch any incoming messages and display them. returning 1 means that the
 * message has not been fully handled and the server should try other methods */
int generic_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *_) {
  // State *state = (State *)_;

  int i;

  printf("MESSAGE _NOT_ HANDLED\n");
  printf("path: <%s>\n", path);
  for (i = 0; i < argc; i++) {
    printf("arg %d '%c' ", i, types[i]);
    lo_arg_pp((lo_type)types[i], argv[i]);
    printf("\n");
  }
  printf("\n");
  fflush(stdout);

  return 1;
}

int matrix_handler(const char *path, const char *types, lo_arg **argv, int argc,
                   void *data, void *_) {
  State *state = (State *)_;

  // XXX get lock

  // update gain values
  //
  for (int i = 0; i < state->size * state->size; ++i) {
    float gain = argv[i]->f;
    if (gain > 1) gain = 1;
    if (gain < 0) gain = 0;
    state->gain[i] = gain;
  }
  state->new = true;
  print(state->gain, state->size);
  fflush(stdout);

  // XXX free lock

  return 0;
}

int connect_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *_) {
  State *state = (State *)_;

  // XXX get lock

  if (argc % 3 != 0) {
    printf("warning: /connect argument list not divisible by 3");
    fflush(stdout);
    return 0;
  }

  // update gain values
  //
  for (int i = 0; i < argc; i += 3) {
    if (types[i] != 'i' || types[i + 1] != 'i' || types[i + 2] != 'f') {
      printf("warning: /connect types (%s) wrong\n", types);
      fflush(stdout);
      return 0;
    }
    int row = argv[i]->i;
    int column = argv[i + 1]->i;
    if (row < 0 || row >= state->size || column < 0 || column >= state->size) {
      printf("warning: /connect arguments (%d, %d) out of bounds\n", row,
             column);
      fflush(stdout);
      return 0;
    }
    float gain = argv[i + 2]->f;
    if (gain > 1) gain = 1;
    if (gain < 0) gain = 0;
    state->gain[row * state->size + column] = gain;
  }
  state->new = true;
  print(state->gain, state->size);
  fflush(stdout);

  // XXX free lock

  return 0;
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
    int size = atoi(argv[1]);
    if (size > 0 && size < 17)  //
      state.size = size;
    else {
      fprintf(stderr, "%d is not a valid matrix size\n", size);
      exit(1);
    }
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
  state.gain = (float *)calloc(sizeof(float), state.size * state.size);
  state.new = true;

  // globals for audio thread only
  gain = (float *)calloc(sizeof(float), state.size * state.size);
  target = (float *)calloc(sizeof(float), state.size * state.size);
  increment = (float *)calloc(sizeof(float), state.size * state.size);

  // make a ring
  //
  for (int k = 0; k < state.size; ++k) {
    int j = (1 + k) % state.size;
    state.gain[k * state.size + j] = 1;
  }

  print(state.gain, state.size);

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

  char ffff[257];
  for (int i = 0; i < 256; ++i)  //
    ffff[i] = 'f';
  ffff[state.size * state.size] = '\0';
  printf("listening on 7777 for /matrix %s\n", ffff);

  lo_server_thread st = lo_server_thread_new("7777", error);
  lo_server_thread_add_method(st, "/matrix", ffff, matrix_handler, &state);
  lo_server_thread_add_method(st, "/connect", NULL, connect_handler, &state);
  lo_server_thread_add_method(st, NULL, NULL, generic_handler, &state);
  lo_server_thread_start(st);

  for (;;) {
#ifdef WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
  }

  lo_server_thread_free(st);
  jack_client_close(client);
  exit(0);
}
