#!/usr/bin/env bash
set -euo pipefail
nv_source=${NVIDIA_SOURCE:?Set NVIDIA_SOURCE to the fully built patched source tree}
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_output=$(mktemp -d /tmp/nvidia-dp-policy.XXXXXX)
includes=(
  src/common/sdk/nvidia/inc src/common/sdk/nvidia/inc/hw
  src/common/shared/inc src/common/inc src/common/modeset
  src/common/unix/common/utils/interface src/common/unix/common/inc
  src/common/displayport/inc src/common/inc/displayport
  src/common/unix/nvidia-push/interface src/common/unix/nvidia-push/include
  src/common/unix/nvidia-3d/interface
  src/nvidia/arch/nvalloc/unix/include
  src/nvidia-modeset/include src/nvidia-modeset/interface
  src/nvidia-modeset/os-interface/include
  src/nvidia-modeset/kapi/interface src/nvidia-modeset/kapi/include
  src/nvidia-modeset/generated src/nvidia-modeset/src/dp
)
compiler_includes=()
for include_dir in "${includes[@]}"; do
  compiler_includes+=("-I${nv_source}/${include_dir}")
done
g++ -no-pie -fno-operator-names -DNDEBUG -DNV_LINUX -DNV_X86_64 -DNV_ARCH_BITS=64 \
  -include "$nv_source/src/common/sdk/nvidia/inc/cpuopsys.h" \
  "${compiler_includes[@]}" "$test_dir/activation-test.cpp" \
  "$nv_source/src/nvidia-modeset/_out/Linux_x86_64/nvdp-connector.o" \
  -Wl,--gc-sections -o "$test_output/activation-test"
"$test_output/activation-test"
printf 'Host test executable retained at %s\n' "$test_output/activation-test"
