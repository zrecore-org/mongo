# Loading `meshvol_ext` into a `mongod` for development

Everything below was run (G5 and MP14c, `RESULTS.md` and exactmesh `docs/RESULTS.md`), on the fork's own `mongod` (v9.1.0, `vulkan-gpu` branch), a non-release build. Linux only: the extension loader does not exist on Windows or macOS builds (`MONGO_HOST_EXTENSIONS_COMPATIBLE`), so on those platforms use the fake host (`host.cpp`, `build.sh`, `run_all.sh`) or a Linux container.

## 1. Build the server (once, hours)
A **non-release** build, so that the extension signature gate is off. Release builds always define `MONGO_CONFIG_EXT_SIG_SECURE` and require a signature made with MongoDB's key; stock 8.2.2, 8.3.0 and 9.0.8 binaries refuse our unsigned library (`RESULTS.md`, MP14b route C), and stock 8.x have different API versions (0.0 and 0.1, ours is 1.0). Our own release builds with our own signing key are not done (key swap unverified). `--allocator=system` is required by the extensions build (`bazel/transitions.bzl`).

## 2. Build the extension (Linux, minutes)
`g++ -std=c++20 -O2 -fPIC -fvisibility=hidden -DMESHVOL_VULKAN_PROBE -shared -Wl,--version-script=...` against the public headers only (`src/mongo/db/extension/public`); see `build.sh` (macOS) and `storage/g5_bundle/build_linux.sh` (Linux). Only `get_mongodb_extension_versions` and `get_mongodb_extension` are exported.

## 3. Configuration
The config directory must be owned by root or by the user running `mongod` (an ownership check; a Windows-mounted directory fails it). One file per extension, `<name>.conf`:
```yaml
# <extensionsConfigPath>/meshvol_ext.conf
sharedLibraryPath: /abs/path/libmeshvol_ext.so
extensionOptions:          # optional; this is how the extension is configured
  gpu: true                # default false. true = probe Vulkan once at startup and log the result
  gpuRequired: true        # default false. true = refuse to start unless a hardware GPU is ready
```
`mongod.conf`:
```yaml
processManagement:
  loadExtensions: [meshvol_ext]
  extensionsConfigPath: /abs/path/conf-dir
setParameter:
  featureFlagExtensionsAPI: true      # default true in this tree; explicit is harmless
```
Options reach the extension as the text `YAML::Dump` makes of the `extensionOptions` map (block style, no trailing newline; `{}` if absent). Only a flat map of `gpu` and `gpuRequired` booleans is accepted: unknown keys, duplicates, non-`true`/`false` values and nested values make loading fail. **Any load failure is fatal to the server by design** (a fatal assertion, SIGABRT). In containers or on WSL set `ulimit -c 0` or `docker run --ulimit core=0` first: each refusal otherwise writes a core dump of about 2 GB (on WSL2 to the Windows temp directory, which filled a C: drive during MP14c). An extension cannot register server parameters, so `setParameter` and `--setParameter` cannot carry this setting.

## 4. What you see
One startup log line, component `EXTENSION-MONGOT`, id 7001400: `meshvolume: gpu requested=<b> ready=<b> engine=cpu reason=<text>`. Reasons: `not requested`, `hardware device present`, `software device only` (lavapipe reports a CPU device), `no physical devices`, `vulkan loader not found`, `vulkan instance creation failed`, `vulkan probe timed out` (10 s limit). **`engine` is always `cpu` today:** the gate only reports capability; `$meshVolume` has no GPU kernel yet. With `gpu: false` (the default) the Vulkan loader is never opened (0 libvulkan mappings in the process). A driver that crashes inside the probe takes the server down (not survivable in-process).

## 5. Run
```
mongod -f mongod.conf
db.coll.aggregate([{$meshVolume: {input: "mesh", output: "vol"}}])
```
Input: a BinData field with the packed mesh (header with counts, binary64 vertices, uint32 indices; `meshvol_core.h`). Output: exact rational string and the correctly rounded binary64, or a per-document error. A bad stage spec is a parse error; a stage that never polls for interrupts runs to completion past `maxTimeMS`, the server cannot interrupt it mid-call (G5 deadline test).

## 6. Licence reminder
The extension is built against SSPL-1.0 headers and stays SSPL; see `../LICENSING.md` before any binary is conveyed.
