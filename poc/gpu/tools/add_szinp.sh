#!/bin/bash
# Adds the SPIR-V SignedZeroInfNanPreserve execution mode (float32) to a compute shader.
# MoltenVK's on-demand fast-math mode compiles shaders without fast math when such a mode is present.
set -e
SDK="$HOME/VulkanSDK/1.3.283.0/macOS/bin"
"$SDK/spirv-dis" "$1" | python3 -c '
import sys,re
t=sys.stdin.read()
t=t.replace("OpCapability Shader","OpCapability Shader\n               OpCapability SignedZeroInfNanPreserve",1)
i=t.find("OpExtInstImport")
if i<0: i=t.find("OpMemoryModel")
i=t.rfind("\n",0,i)+1
t=t[:i]+"OpExtension \"SPV_KHR_float_controls\"\n               "+t[i:]
m=re.search(r"OpEntryPoint GLCompute (%\w+)",t)
ep=m.group(1)
t=re.sub(r"(OpExecutionMode "+re.escape(ep)+r" LocalSize[^\n]*\n)",r"\1               OpExecutionMode "+ep+" SignedZeroInfNanPreserve 32\n",t,1)
sys.stdout.write(t)' | "$SDK/spirv-as" --target-env vulkan1.2 -o "$2" -
"$SDK/spirv-val" --target-env vulkan1.2 "$2"
