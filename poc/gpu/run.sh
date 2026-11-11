#!/bin/bash
# Runs a built POC binary with the MoltenVK ICD configured: ./run.sh poc0_probe [args...]
HERE="$(cd "$(dirname "$0")" && pwd)"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$HOME/VulkanSDK/1.3.283.0/macOS/share/vulkan/icd.d/MoltenVK_icd.json}"
exec "$HERE/build/$1" "${@:2}"
