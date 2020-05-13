# Jack Matrix Mixer

Q: Why?? Jack _is_ a matrix mixer! You can route anything to anything with Jack.

A: Well, we need something that cross-fades when we change the matrix connections. Jack would "click" when connections are added or removed. Also, we designed this mixer to help us practice and perform _Ensemble Feedback Instruments_ (2015).

<hr />

Start the mixer with the CPI `matrix`. This takes a single argument, the length of a side of the _square_ matrix.

Control the matrix with OSC messages such as these:

    /matrix ffff 0.1 0.3 0.5 0.7
    /connect iif 0 1 0.333
    /connect iifiif 0 1 0.0 1 0 1.0

Where the number of `f` in the type list of the `/matrix` message is $N \cdot N$ where $N$ is the length of a side of the square matrix.

