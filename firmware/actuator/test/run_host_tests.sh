#!/bin/sh
# Host-side unit test for the pure motion maths (no ESP-IDF needed).
set -e
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -O1 -o /tmp/tn_motion_test test_motion_math.c ../main/motion_math.c -lm
/tmp/tn_motion_test
