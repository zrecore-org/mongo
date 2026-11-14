// Trivial helper shared library the extension dlopen()s at runtime (stand-in for a Vulkan loader or a mesh library).
#include "mesh_format.h"
#include <string>
extern "C" __attribute__((visibility("default"))) int mesh_helper_volume(const uint8_t* mesh, uint64_t len, double* out) {
    std::string err;
    return meshfmt::volume(mesh, (size_t)len, *out, err) ? 0 : 1;
}
