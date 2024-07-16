BZSim
====


License & Copyright
-------------------

BZsim is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.

If you use this software in your research, we request that you reference the BZsim paper: "BZSim: Fast, Large-scale Microarchitectural Simulation with Detailed Interconnect Modeling", P. Strikos, A. Ejaz, I. Sourdis, ISPASS 2024

Setup
-----


dependencies: gcc, pin, scons, libconfig, libhdf5, libelfg0

1. Clone BZSim repository

2. Download Pin 2.14 [here](https://software.intel.com/sites/landingpage/pintool/downloads/pin-2.14-71313-gcc.4.4.7-linux.tar.gz).


3. Run `source env.sh` to set the required enviroment variables which assumes that pin, hd5 and dramsim2 are all located inside bzsim/. Alternatively, follow steps 4 and 5.

4. Set the following enviroment variables:
    `ZSIMPATH`
    `PINPATH`
    `LIBCONFIGPATH`
    `HDF5PATH`
    `BOOKSIMPATH`

   Optionally, download and build DRAMSim2, and set `DRAMSIMPATH`. DRAMSim2 is used for the basic example.


5. Add the booksim library to the dynamic shared library path:
    `export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$BOOKSIMPATH/src/`

6. Compile BookSim: `(cd booksim2/src && make)`

7. Compile ZSim: `(cd zsim && scons)`

8. Launch a test run for 2 cores, using DRAMSim2: 
    `export OMP_NUM_THREADS=2` 
    
    `cd zsim/' 
     
    `./build/opt/zsim tests/bzsimsimple.cfg`
