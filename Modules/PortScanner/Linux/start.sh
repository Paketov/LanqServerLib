#!/bin/sh

LD_LIBRARY_PATH=$(dirname "$0")/../../../Linux
export LD_LIBRARY_PATH
$(dirname "$0")/lqscan
