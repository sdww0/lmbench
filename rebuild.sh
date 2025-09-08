#! /bin/bash

make
rm -rf /usr/local/benchmark/lmbench/
cp -r ./bin/x86_64-linux-gnu/ /usr/local/benchmark/lmbench
