///////////////////////////////////////////////////////////////////////////////
//// Cross-Fading Matrix Mixer ////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
// Karl Yerkes / 2020-05-xx
//
// the default "ring" topology..
//
//      COLUMNS
// I  ╔═╤═╤═╤═╤═╗
//   ₁║ │░│ │ │ ║
// N  ╟─┼─┼─┼─┼─╢ R
//   ₂║ │ │░│ │ ║
// P  ╟─┼─┼─┼─┼─╢ O
//   ₃║ │ │ │░│ ║
// U  ╟─┼─┼─┼─┼─╢ W
//   ₄║ │ │ │ │░║
// T  ╟─┼─┼─┼─┼─╢ S
//   ₅║░│ │ │ │ ║
// S  ╚═╧═╧═╧═╧═╝
//     ₁ ₂ ₃ ₄ ₅
//      OUTPUTS
//

///////////////////////////////////////////////////////////////////////////////
//// INCLUDES /////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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
#include <pthread.h>

// how to index the gain data
#define INDEX(n, r, c) (r * n + c)

///////////////////////////////////////////////////////////////////////////////
//// STRUCTS //////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
//// GLOBALS //////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

jack_client_t *client;
pthread_mutex_t mutex;

static float *gain;
static float *target;
static float *increment;

///////////////////////////////////////////////////////////////////////////////
//// AUDIO CALLBACK ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int process(jack_nframes_t N, void *_) {
  State *state = (State *)_;

  if (pthread_mutex_trylock(&mutex) == 0) {
    if (state->new) {
      state->new = false;
      for (int i = 0; i < state->size * state->size; ++i) {
        target[i] = state->gain[i];
        increment[i] = (target[i] - gain[i]) / (0.1 * 44100);
      }
    }

    pthread_mutex_unlock(&mutex);
  }

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
  for (int column = 0; column < state->size; ++column) {
    jack_default_audio_sample_t *output = state->o[column];
    for (int n = 0; n < N; ++n) {
      output[n] = 0;
    }

    for (int row = 0; row < state->size; ++row) {
      int index = INDEX(state->size, row, column);
      float g = gain[index];
      float t = target[index];
      float i = increment[index];

      jack_default_audio_sample_t *input = state->i[row];
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
  for (int column = 0; column < state->size; ++column) {
    jack_default_audio_sample_t *mix = state->m[column];
    for (int n = 0; n < N; ++n) {
      mix[n] = 0;
    }

    for (int row = 0; row < state->size; ++row) {
      if (row == column) continue;

      jack_default_audio_sample_t *input = state->i[row];
      for (int n = 0; n < N; ++n) {
        mix[n] += input[n];
      }
    }
  }

  return 0;
}

///////////////////////////////////////////////////////////////////////////////
//// HELPER FUNCTIONS /////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const char *character(float v) {
  const char *block[9] = {"░", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  int i = (v * 8);
  if (i < 0) i = 0;
  if (i > 8) i = 8;
  return block[i];
}

void print(float *matrix, int size) {
  for (int i = 0; i < size; ++i)  //
    printf("─");
  printf("\n");
  for (int row = 0; row < size; ++row) {
    for (int column = 0; column < size; ++column)  //
      printf("%s", character(matrix[INDEX(size, row, column)]));
    printf("\n");
  }
}

///////////////////////////////////////////////////////////////////////////////
//// OTHER CALLBACKS //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void jack_shutdown(void *arg) { exit(1); }

static void signal_handler(int sig) {
  jack_client_close(client);
  fprintf(stderr, "signal received, exiting ...\n");
  exit(0);
}

///////////////////////////////////////////////////////////////////////////////
//// NETWORK CALLBACKS ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void error(int num, const char *msg, const char *path) {
  printf("liblo server error %d in path %s: %s\n", num, path, msg);
  fflush(stdout);
}

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

  // BLOCKING
  if (pthread_mutex_lock(&mutex) != 0) {
    printf("mutex lock failed\n");
    fflush(stdout);
    exit(1);
  }

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

  pthread_mutex_unlock(&mutex);

  return 0;
}

int connect_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *_) {
  State *state = (State *)_;

  // BLOCKING
  if (pthread_mutex_lock(&mutex) != 0) {
    printf("mutex lock failed\n");
    fflush(stdout);
    exit(1);
  }

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
    state->gain[INDEX(state->size, row, column)] = gain;
  }
  state->new = true;
  print(state->gain, state->size);
  fflush(stdout);

  pthread_mutex_unlock(&mutex);

  return 0;
}

///////////////////////////////////////////////////////////////////////////////
//// MAIN /////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  if (pthread_mutex_init(&mutex, NULL) != 0) {
    printf("\n mutex init failed\n");
    return 1;
  }

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
  for (int column = 0; column < state.size; ++column) {
    int row = (1 + column) % state.size;
    state.gain[INDEX(state.size, row, column)] = 1;
  }

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
    sprintf(port_name, "input_%d", 1 + k);
    state.in[k] = jack_port_register(client, port_name, JACK_DEFAULT_AUDIO_TYPE,
                                     JackPortIsInput, 0);
    if (state.in[k] == NULL) {
      fprintf(stderr, "no more JACK ports available\n");
      exit(1);
    }

    // outputs
    //
    sprintf(port_name, "output_%d", 1 + k);
    state.out[k] = jack_port_register(
        client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (state.out[k] == NULL) {
      fprintf(stderr, "no more JACK ports available\n");
      exit(1);
    }

    // mixes (also outputs)
    //
    sprintf(port_name, "mix_%d", 1 + k);
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

  lo_server_thread st = lo_server_thread_new("7777", error);
  lo_server_thread_add_method(st, "/matrix", ffff, matrix_handler, &state);
  lo_server_thread_add_method(st, "/connect", NULL, connect_handler, &state);
  lo_server_thread_add_method(st, NULL, NULL, generic_handler, &state);
  lo_server_thread_start(st);
  printf("listening on 7777 for /matrix %s\n", ffff);

  // XXX maybe do this with an OSC message to self?
  print(state.gain, state.size);

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
