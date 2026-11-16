// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 zrecore-org and exactmesh contributors
// MP16: the few operating-system calls the extension and its test host need, for POSIX (dlopen) and Windows (LoadLibrary): open a library, look up a symbol, read the last loader error once.
#pragma once
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
namespace pc {
using Lib = HMODULE;
inline Lib openLib(const char* path) { return LoadLibraryA(path); }
inline void* sym(Lib h, const char* name) { return reinterpret_cast<void*>(GetProcAddress(h, name)); }
inline std::string libError() { return "Win32 error " + std::to_string((unsigned long)GetLastError()); }
}
#define MESHVOL_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
namespace pc {
using Lib = void*;
inline Lib openLib(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }   // same flags as src/mongo/platform/shared_library_posix.cpp
inline void* sym(Lib h, const char* name) { return dlsym(h, name); }
inline std::string libError() { const char* e = dlerror(); return e ? e : "unknown loader error"; }   // dlerror() clears itself: read it once
}
#define MESHVOL_EXPORT __attribute__((visibility("default")))
#endif
