BZSim System Simulator with Detailed Interconnect Modelling
====


License & Copyright
-------------------

BZsim is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.

If you use this software in your research, we request that you reference the BZsim paper: "BZSim: Fast, Large-scale Microarchitectural Simulation with Detailed Interconnect Modeling", P. Strikos, A. Ejaz, I. Sourdis, ISPASS 2024

Setup
-----


0. Dependencies: gcc, g++, pin, scons, libconfig/libconfig++, libhdf5, libelf, flex, bison

1. Clone BZSim repository

2. Download Pin 2.14 [here](https://software.intel.com/sites/landingpage/pintool/downloads/pin-2.14-71313-gcc.4.4.7-linux.tar.gz).

3. Clone the DRAMSim2 repository from [here](https://github.com/umd-memsys/DRAMSim2)

4. Run `source env.sh` to set the required enviroment variables which assumes that pin, hd5 and dramsim2 are all located inside bzsim/. Alternatively, follow steps 5 and 6.

5. Set the following enviroment variables:
    `PINPATH`
    `LIBCONFIGPATH`
    `HDF5PATH`
    `BOOKSIMPATH`
    `DRAMSIMPATH`

6. Add the booksim library to the dynamic shared library path:
    `export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$BOOKSIMPATH/src/`

7. Compile DRAMSim2: 

    First set the following flags in DRAMSim2 Makefile : `-D_GLIBCXX_USE_CXX11_ABI=0 -fabi-version=2`
    and compile: `(cd DRAMSim2 && make libdramsim.so)`

8. Compile BookSim: `(cd booksim2/src && make)`

9.  Compile ZSim: `(cd zsim && scons)`

10. Launch a test run for 2 cores: 

    `export OMP_NUM_THREADS=2` 
    
    `cd zsim/' 
     
    `./build/opt/zsim tests/bzsimsimple.cfg`
