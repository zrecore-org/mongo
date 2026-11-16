# Loading `meshvol_ext` into a `mongod` for development

Everything below was run (G5 and MP14c, `RESULTS.md` and exactmesh `docs/RESULTS.md`), on the fork's own `mongod` (v9.1.0, `vulkan-gpu` branch), a non-release build. Linux only: the extension loader does not exist on Windows or macOS builds (`MONGO_HOST_EXTENSIONS_COMPATIBLE`), so on those platforms use the fake host (`host.cpp`, `build.sh`, `run_all.sh`) or a Linux container.

## 1. Build the server (once, hours)
A **non-release** build, so that the extension signature gate is off. Release builds always define `MONGO_CONFIG_EXT_SIG_SECURE` and require a signature made with MongoDB's key; stock 8.2.2, 8.3.0 and 9.0.8 binaries refuse our unsigned library (`RESULTS.md`, MP14b route C), and stock 8.x have different API versions (0.0 and 0.1, ours is 1.0). Our own release builds with our own signing key are not done (key swap unverified). `--allocator=system` is required by the extensions build (`bazel/transitions.bzl`).

Runtime prerequisites for the fork `mongod` on a bare Ubuntu 24.04 x86_64: `libcurl4 libssl3 liblzma5 ca-certificates` (without `libcurl4` it exits 127). For the Vulkan probe to find a loader add `libvulkan1`; add `mesa-vulkan-drivers` for lavapipe (a software device; `gpu: true` then reports `software device only`).

## 2. Build the extension (Linux, minutes)
Needs `g++` and bash only. The sources are in the bundle `g5_bundle/` (top directory of `mp14c_bundle2.tgz`): `src/ext/` (the extension), `src/boost/` (vendored Boost.Multiprecision, header only), `src/vkinc/` (Vulkan headers), `src/inc/` (the Extensions API public headers `src/mongo/db/extension/public`). Run `bash g5_bundle/build_linux.sh` (bash: it uses process substitution); it builds `g5_bundle/out/libmeshvol_ext.so` in about 10 s with `-std=c++20 -O2 -fPIC -fvisibility=hidden -DMESHVOL_VULKAN_PROBE -shared` and a version script, so only `get_mongodb_extension_versions` and `get_mongodb_extension` are exported. The bundle may ship a prebuilt `out/libmeshvol_ext.so`; delete it to build for real. On macOS `build.sh` builds the `.dylib` for the fake host only.

## 3. Configuration
The config directory must be owned by root or by the user running `mongod` (an ownership check; a Windows-mounted directory fails it). One file per extension, `<name>.conf`:
```yaml
# <extensionsConfigPath>/meshvol_ext.conf
sharedLibraryPath: /abs/path/libmeshvol_ext.so
extensionOptions:          # optional; this is how the extension is configured
  gpu: true                # default false. true = probe Vulkan once at startup and log the result
  gpuRequired: true        # default false. true = refuse to start unless a hardware GPU is ready; also refused if gpu is not true
```
`mongod.conf` (the minimum that starts; `storage.dbPath`, `net` and `systemLog` are ordinary mongod settings, shown because mongod does not start without a writable `dbPath`):
```yaml
processManagement:
  loadExtensions: [meshvol_ext]
  extensionsConfigPath: /abs/path/conf-dir
setParameter:
  featureFlagExtensionsAPI: true      # default true in this tree; explicit is harmless
net: {port: 27555, bindIp: 127.0.0.1}
storage: {dbPath: /abs/path/db}       # must exist and be writable
systemLog: {destination: file, path: /abs/path/mongod.log}
```
Options reach the extension as the text `YAML::Dump` makes of the `extensionOptions` map (block style, no trailing newline; `{}` if absent). Only a flat map of `gpu` and `gpuRequired` booleans is accepted: unknown keys, duplicates, values other than `true`/`false` (`yes`, `True`, `1`, empty, `~`) and nested values make loading fail. A quoted `"true"` is accepted: `YAML::Dump` strips the quotes, so the extension cannot tell it from `true`. **Any load failure is fatal to the server by design** (a fatal assertion, SIGABRT). Each refusal can write a core dump of about 2 GB: on WSL2 it went to the Windows temp directory and filled a C: drive during MP14c. `ulimit -c 0` and `docker run --ulimit core=0` do NOT prevent it on WSL2: the WSL VM's `core_pattern` is a pipe handler (`|/wsl-capture-crash ...`), and for a pipe handler the kernel ignores a core limit of 0; only a limit of exactly 1 byte makes the kernel skip the handler. So run containers with `docker run --ulimit core=1:1` (and do not run `ulimit -c 0` inside: it would lower the limit back to 0 and re-enable the dump; use `prlimit --core=1:1 --pid $$` instead), check `Max core file size` in `/proc/<pid>/limits`, and keep the `.wslconfig` `crashDumpFolder` redirect and `maxCrashDumpCount` as the safety net (verified experimentally on WSL 3.0.5, kernel 6.18; the real `mongod` abort under `core=1:1` is not yet confirmed). The log reports the status with an unrelated code name (`RoleNotFound`, `CannotBackfillArray`, ...): cosmetic, read the message text. An extension cannot register server parameters, so `setParameter` and `--setParameter` cannot carry this setting.

## 4. What you see
One startup log line, component `EXTENSION-MONGOT`, id 7001400: `meshvolume: gpu requested=<b> ready=<b> engine=cpu reason=<text>`. Reasons: `not requested`, `hardware device present`, `software device only` (lavapipe reports a CPU device), `no physical devices`, `vulkan loader not found`, `vulkan instance creation failed`, `vulkan probe failed` (the probe threw), `vulkan probe timed out` (10 s limit). **`engine` is `gpu` when the gate says the device path is available** (`gpu: true` and a hardware device): `$meshContains` then uses the device for documents with at least `gpuMinPoints` points (stage option, default 250,000, about the M1 break-even; `engine: "cpu"` forces the CPU; `cpuThreads`, default 1, spreads the CPU path of one document over N threads) and falls back to the CPU, permanently until restart, on any device failure, timeout or shadow-check mismatch. `$meshVolume` has no GPU kernel and is always CPU. Speed: in the document-size-bounded regime the device path is break-even at best (see exactmesh `docs/RESULTS.md`, MP14d). With `gpu: false` (the default) the Vulkan loader is never opened (0 libvulkan mappings in the process). A driver that crashes inside the probe takes the server down (not survivable in-process).

## 5. Run
```
mongod -f mongod.conf
db.coll.aggregate([{$meshVolume: {input: "mesh", output: "vol"}}])
```
Stage: `{$meshVolume: {input: "mesh", output: "vol"}}` (defaults `mesh` and `meshVolume`; other keys are test knobs). Input: a BinData **subtype 0** field. Layout, little-endian: u32 magic `0x3148534d` (`'MSH1'`), u32 `nv`, u32 `nt`, then `nv*3` binary64 vertex coordinates, then `nt*3` u32 triangle indices (`meshvol_core.h`). The mesh must be a closed, oriented 2-manifold with outward counter-clockwise triangles: an open mesh gives a per-document error 8, a repeated directed edge or degenerate triangle error 9. Limits: `nt` at most 2^24, `nt == 0` is error 14 (empty mesh). Output: the document gets the field `vol: {exact: "<reduced p/q, or p when the denominator is 1>", approx: <binary64 correctly rounded from the exact value>}`, or `vol: {error: {code, reason}}` with the codes of `meshvol::Err` (1 missing field, 2 not BinData, 3 bad subtype, 4 bad header, 5 size mismatch, 6 index out of range, 7 non-finite, 8 open mesh, 9 non-manifold, 10 too large, 14 empty). Example with pymongo: `from bson.binary import Binary` (not `pymongo.binary`); `{"_id": 1, "mesh": Binary(packed, 0)}`; a unit cube gives `{'exact': '1', 'approx': 1.0}`.

## 6. Licence reminder
The extension is built against SSPL-1.0 headers and stays SSPL; see `../LICENSING.md` before any binary is conveyed.
