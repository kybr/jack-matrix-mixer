# Jack Matrix Mixer

Q: Why?? Jack _is_ a matrix mixer! You can route anything to anything with Jack.

A: Well, we need something that cross-fades when we change the matrix connections. Jack would "click" when connections are added or removed. Also, we designed this mixer to help us practice and perform _Ensemble Feedback Instruments_ [2015] (Muhammad Hafiz Wan Rosli, Karl Yerkes, Timothy Wood, Hannah Wolfe, Charlie Roberts, Anis Haron, Fernando Rincón Estrada, Matthew Wright).

<hr />

Start the mixer with the CLI `matrix`. This takes a single argument, the length of a side of the _square_ matrix.

    $ git clone https://github.com/kybr/jack-matrix-mixer
    $ cd jack-matrix-mixer
    $ make # works on Linux and macOS; no Windows yet
    $ ./matrix 5 # for a 5x5 matrix
    $ ./test 5 # (on a separate terminal) provides a sine signal array

Control the matrix with OSC messages such as these:

    /matrix ffff 0.1 0.3 0.5 0.7
    /connect iif 0 1 0.333
    /connect iifiif 0 1 0.0 1 0 1.0

Where the number of `f` in the type list of the `/matrix` message is N * N where N is the length of a side of the square matrix.

## TODO

- test with JackTrip
- copy OSC protocol from the Max patch
- add automatic connection to JackTrip ports
- support several common topology
  + ring, star, trios, full

### DONE

- implement cross-fading
- note where to put locks
- implement OSC control (of absolute matrix state)
- integrate OSC library
- fix clicks at the block rate
  + you have to clear the output buffer before accumulating
- implement matrix mixing
- adapt to allow CLI option for N inputs/outputs
- remove globals
- make a compiling, running jack client app
- make a Github repository
- choose a license
- start a README
