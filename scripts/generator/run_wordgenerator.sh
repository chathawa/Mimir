#!/bin/bash
./sub_wordgenerator.sh triangular words /scratch/rice/g/gao381/wordcount/triangular/onenode \
  "32M 64M 128M 256M 512M 1G 2G 4G 8G 16G 32G" \
  "33554432 67108864 134217728 268435456 536870912 1073741824 2147483648 4294967296 8589934592 17179869184 34359738368" \
  "20 20 20 20 20 20 20 20 20 20 20" 5
