#!/bin/bash

cd build
./premake5.osx gmake
cd ..
make
./bin/Debug/AdvancedProgrammingLinkedListVisualizer
