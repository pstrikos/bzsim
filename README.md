BZSim
====


License & Copyright
-------------------

Setup
-----


dependencies: gcc, pin, scons, libconfig, libhdf5, libelfg0

1. Clone BZSim repository

2. Download Pin 2.14 [here](https://software.intel.com/sites/landingpage/pintool/downloads/pin-2.14-71313-gcc.4.4.7-linux.tar.gz). T

3. Set the folloing enviroment variables:
    `ZSIMPATH`
    `PINPATH`
    `LIBCONFIGPATH`
    `HDF5PATH`
    `BOOKSIMPATH`

4. Add the booksim library to the dynamic shared library path:
    `export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$BOOKSIMPATH/src/`

5. Compile booksim: `(cd booksim2/src && make)`

6. Compile zsim: `(cd zsim && scons)`

7. Launch a test run: 
    `cd zsim/'
    `./build/opt/zsim tests/bzsimsimple.cfg`
