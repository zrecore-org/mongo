#!/bin/sh
# One locked session for the POC 2 timing sweep and accuracy run. Output goes to out/.
cd "$(dirname "$0")" || exit 1
echo "GPU utilization before: $(ioreg -r -d 1 -w0 -c IOAccelerator | grep -o '"Device Utilization %"=[0-9]*')"
./run.sh poc2_sum bench 1  || exit 1
./run.sh poc2_sum bench 16 || exit 1
./run.sh poc2_sum accuracy || exit 1
./run.sh poc3_select       || exit 1
# df64 without `precise` and MoltenVK fast math off (pg-strom's setting): speed and correctness vs the `precise` build
export MVK_CONFIG_FAST_MATH_ENABLED=0 POC_SHADER_SUFFIX=_plain
./run.sh poc2_sum bench 1  || exit 1
./run.sh poc2_sum accuracy || exit 1
echo "GPU utilization after: $(ioreg -r -d 1 -w0 -c IOAccelerator | grep -o '"Device Utilization %"=[0-9]*')"
