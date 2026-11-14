# MP14a: can a mesh feature live in a MongoDB extension? (spike report)

Date: 2026-11-13. Branch `vulkan-gpu`. Scope: compile-and-harness only. **No mongod was built or run, no bazel, no GPU, no CI.** Everything below was
exercised against a fake host written for this POC, not against the real server. Artifacts live under `storage/mesh/extension/` (external volume);
sources under `poc/gpu/mesh/extension/`.

## Verdict for gate G5: viable with caveats

An extension can carry a `$meshMeasure`-style transform stage, load through the documented two-phase protocol, batch many meshes per compute dispatch,
report errors, cooperate with deadlines/killOp, `dlopen` other libraries and own threads. It can **not** add expression operators, index types, BSON types
or commands, so the feature surface is "new aggregation stages" only (see (g)). Caveats that matter for the decision:

1. **No C++ SDK is usable**: the SDK links `mongo/base` (`sdk/BUILD.bazel:21` depends on `//src/mongo/db:server_base`; README "Important Note ... SERVER-107651"). I implemented the raw C ABI directly (about 630 lines incl. a mini BSON reader/writer). Only Rust is supported in production ("Only Rust extensions are supported in production", `README.md`/`CLAUDE.md`), and that Rust SDK is not in this repo nor on crates.io (see Rust section).
2. **The public headers do not compile standalone.** Both `api.h:9` and `api_contract.h:9` fall back to `#define[[MONGO_MOD_PUBLIC]]` when `mongo/util/modules.h` is absent, which is a hard error (`macro name must be an identifier`). With `-I src` the headers instead include the real server `modules.h`, which includes `mongo/platform/compiler.h`. I used symlinks to the two headers plus a 3-line shim `mongo/util/modules.h` defining `MONGO_MOD_PUBLIC` empty. They are also not valid C (`::MongoExtensionLogType` qualifiers at `api.h:245,341`, `enum X : uint8_t`), so "C API" means C++-compilable; Rust needs hand-written or C++-mode bindgen bindings.
3. **Every vtable slot is mandatory** (the host `tassert`s non-null on each, see the list below); "optional" only means a stub returning OK with a null output.
4. **One document per `get_next`**; batching is internal buffering (works, tested).
5. **The host never calls `open()`/`reopen()`/`close()`** in production code (no caller found in `src/mongo/db`), so GPU/device init must be lazy inside `get_next`, and teardown happens only in `destroy()`.
6. **No preemption**: deadlines/killOp are visible only if the extension polls. A non-polling 2 s batch ran to completion past a 300 ms deadline.
7. **An exception escaping a C entry point aborts the process** (SIGABRT in the test). A GPU driver crash inside the extension kills mongod too (in-process plugin, no isolation).
8. Not verified: real mongod load path (signature check, `.conf`, ownership checks, `featureFlagExtensionsAPI`), Linux/GCC build, allocator mismatch (tcmalloc host vs `--allocator=system` extension), Vulkan loader on a real device, thread-safety of the host logger, sharded behaviour.

## What was built (all compiled and run)

| File | Purpose |
|---|---|
| `mesh_extension.cpp` | The extension. Includes only `public/api.h`, `public/api_contract.h` (via symlinks in `storage/mesh/extension/include/`) and std/POSIX. Exports exactly `get_mongodb_extension_versions` and `get_mongodb_extension` (verified with `nm -gU`, see raw output). |
| `mini_bson.h`, `mesh_format.h` | Own BSON reader/writer (double, string, doc, array, binData, bool, int32/64; skips the rest) and the packed mesh format. |
| `mesh_helper.cpp` | Trivial helper shared library the extension `dlopen`s at runtime (stand-in for the Vulkan loader or a mesh library). |
| `fake_host.cpp` | Fake host: portal, services, query-execution context (deadline/killOp), host source stage, host-allocated status/byte buffers, lifecycle driver, 81 checks. |
| `build.sh` | Builds and runs everything: plain, and once under `-fsanitize=address,undefined`. |

**Mesh format** (BinData subtype 0, little-endian, packed): `u32 nv | u32 nt | float64 xyz[nv*3] | u32 tri[nt*3]`, total `8 + 24*nv + 12*nt` bytes. Output volume is
`sum(a . (b x c)) / 6` in plain double (no exactness claim; this POC is about packaging).

**Stage**: `{$meshMeasure: {field: "mesh", as: "volume", batch: N, ...test knobs}}`. Test knobs: `mode` (normal | throw | code | escape | slow | slow_poll), `out` (buf | view | alt),
`lib` (shared library to dlopen), `probe`, `ticker`, `worker`, `sleepMs`.

Commands run (all through `./build.sh`, one job at a time, `TMPDIR=storage/tmp`):

```
clang++ -std=c++20 -Wall -Wextra -fvisibility=hidden -fPIC -g -O1 -I<storage>/include -I. -Wno-exceptions \
        -dynamiclib -Wl,-exported_symbols_list,exports.txt mesh_extension.cpp -o mesh_extension_prod.dylib   # 2 exported symbols
clang++ ... -DMESH_TEST_HOOKS ... exports_test.txt ... -o mesh_extension.dylib      # + mesh_ext_test_counters (test-only export)
clang++ ... -dynamiclib mesh_helper.cpp -o libmeshhelper.dylib
clang++ -std=c++20 -g fake_host.cpp -o fake_host
# same three with -fsanitize=address,undefined -fno-omit-frame-pointer -> asan_*
fake_host mesh_extension.dylib libmeshhelper.dylib
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 asan_fake_host asan_mesh_extension.dylib asan_libmeshhelper.dylib
```
Platform: macOS arm64, Apple clang 21, libc++. Result: **81 passed, 0 failed** in both the plain and the ASan+UBSan run (no sanitizer report). The
"extension stdout" line `libc++abi: terminating due to uncaught exception` in section D is the intended `mode=escape` child process (forked), not a failure.
Extension dynamic dependencies: only `libc++` and `libSystem` (no mongo libs).

## Mandatory vtable entries (host asserts non-null; the extension must provide all of these)

Source of truth: each `assertVTableConstraints` in `src/mongo/db/extension/shared/handle/...` (and `host_connector/handle/extension_handle.h:47`). The fake host re-implements the same
checks (`A(...)` audit, 50 slots, 0 NULL). Objects returned by the extension and what the extension must supply:

| Object (header ref) | Slots the host requires non-null |
|---|---|
| `MongoExtension` (`extension_handle.h:47`) | `initialize` |
| `AggStageDescriptor` (`stage_descriptor.h:62`) | `get_name`, `get_client_type`, `parse` (no `destroy`: extension-owned, never freed) |
| `AggStageParseNode` (`parse_node.h:111`) | `destroy`, `get_name`, `get_query_shape`, `expand`, `clone`, `to_bson_for_log` |
| `ExpandedArrayContainer` (`expanded_array_container.h:46`) | `destroy`, `size`, `transfer` |
| `AggStageAstNode` (`ast_node.h:76`) | `destroy`, `get_name`, `get_properties`, `promote`, `clone`, `get_first_stage_view_application_policy`, `bind_resolved_namespace` |
| `LogicalAggStage` (`logical.h:117`) | `destroy`, `get_name`, `serialize`, `explain`, `compile`, `get_distributed_plan_logic`, `clone`, `set_vector_search_limit_for_optimization_deprecated`, `evaluate_pipeline_rewrite_rule_precondition`, `evaluate_pipeline_rewrite_rule_transform`, `get_filter`, `apply_pipeline_suffix_dependencies`, `get_sort_pattern`, `skip_stream`, `get_docs_needed_bounds` |
| `ExecAggStage` (`executable_agg_stage.h:53`) | `destroy`, `get_next`, `get_name`, `create_metrics`, `set_source`, `open`, `reopen`, `close`, `explain` |
| `OperationMetrics` (`operation_metrics_handle.h:47`) | `destroy`, `serialize` (and `update` in api.h) |
| `ByteBuf` (`byte_buf_handle.h:46`) | `destroy`, `get_view` |
| `Status` (`extension_status.h`, `StatusAPI::assertVTableConstraints`) | `destroy`, `get_code`, `get_reason`, `set_code`, `set_reason`, `clone` |

"Real" work vs stubs in this extension: parse (spec validation), query shape (calls the host's `serialize_field_path`), expand (one AstNode), `get_properties`, promote (copies the namespace out of the catalog context), compile, `get_next`,
`set_source`, `explain`. Pure stubs returning OK with null/false outputs: DPL (`nullptr` = runs fully on the shards), rewrite-rule precondition/transform (false), `get_filter`, `get_sort_pattern`, `skip_stream`, `apply_pipeline_suffix_dependencies`,
`set_vector_search_limit_for_optimization_deprecated`, `bind_resolved_namespace`; `get_docs_needed_bounds` returns `{effect: "noEffect"}`.

**What the host must provide** (all implemented in `fake_host.cpp`): the `MongoExtensionHostServices` vtable (`api.h:1332`, 7 slots: logger, `user_asserted`, `tripwire_asserted`, `mark_idle_thread_block`, three `create_*` host-stage factories); the `MongoExtensionHostPortal` (`api.h:1284`: `register_stage_descriptor`,
`get_extension_options` (a YAML text, not BSON: `sdk/host_portal.h:75` parses it with `YAML::Load`), `register_stage_rules`); a `MongoExtensionQueryExecutionContext` per `get_next` (`api.h:900-950`: `check_for_interrupt`, `get_metrics`, `get_deadline_timestamp_ms`, `get_host_metrics`, `set_batch_size`);
a `MongoExtensionHostQueryShapeOpts` for query shapes; a source `MongoExtensionExecAggStage` handed over with `set_source` (the extension calls its `get_next`); host-allocated `Status` and `ByteBuf` objects with working `destroy`; the version negotiation and the `extension->version == negotiated` check.

## Answers (a)-(g)

### (a) Many documents per `get_next`, or one?
**One.** `MongoExtensionGetNextResult` (`api.h:799-803`) holds a single `code` and one `resultDocument` container (plus one metadata container); the host loop is one `doGetNext` per document (`host/extension_stage.cpp:92-120`, which also `getOwned()`s a copy of every returned BSON).
Batching many meshes into one dispatch is therefore **internal buffering**: inside one `get_next` the stage pulls up to N inputs from the host source (each source `get_next` is also one document), computes them in one dispatch, then drains the buffer one document per call.
`set_batch_size` (`api.h:949`) is not a way out: it only tunes how many docs the host pulls per getMore for a source and the host rejects it elsewhere with uassert 13150705 (`host_connector/adapter/query_execution_context_adapter.cpp:66-70`; mirrored and tested).
Tested (section C): 20 docs, `batch=8` -> exactly 21 `get_next` calls (20 + EOF), the first output appears only after 8 source pulls, explain shows `dispatches=3 maxBatch=8 sourcePulls=21`; `batch=1/5/20/1000` give 20/4/1/1 dispatches and identical output; the default batch also comes from a portal option (`defaultBatch: 4` in the YAML options). Costs: input bytes are copied (a `kByteView` from the host dies at the next source call, `api.h:820-832`), the whole batch fails together on one bad document (no partial output), `kPauseExecution` mid-batch flushes a short batch.
Implication: a GPU dispatch of K meshes needs K documents resident in the extension; BinData is bounded by the 16 MiB BSON document limit, so one document holds at most roughly 16 MiB / (24 B per vertex + 12 B per triangle) (about 350k vertices for a closed surface; my arithmetic, not tested).

### (b) Deadlines / killOp / interrupts
Surfaced **only by pull**, through `MongoExtensionQueryExecutionContext` (`api.h:900-930`): `check_for_interrupt(ctx, queryStatus)` writes the interruption (killOp, maxTimeMS expiry, shutdown: whatever `OperationContext::checkForInterruptNoAssert()` reports, `host/query_execution_context.h:28`) into an **extension-owned** status object through its `set_code`/`set_reason` and leaves it OK otherwise; `get_deadline_timestamp_ms` returns the deadline as Unix ms (`INT64_MAX` when none; `Date_t::asInt64()`, `query_execution_context.h:39`), as in `test_examples/assert_deadline_increased_after_batch.cpp` which asserts the deadline is refreshed at each getMore batch boundary. `explain()` must honour deadlines too (`api.h:649`).
The context object is built on the host stack for each call (`extension_stage.cpp:94-102`): never retain it, so extension-owned threads cannot poll.
Tested (section E): per-batch poll with a past deadline -> code 50 before any work; **non-polling 2 s batch with a +300 ms deadline returned OK after 2005 ms** (the host cannot preempt; it notices only afterwards); polling in 20 ms slices -> code 50 after 312 ms; killOp from another thread at +200 ms -> code 11601 after 216 ms.
For a GPU stage this means: chunk dispatches so each chunk is short, poll between chunks, and treat cancellation of in-flight device work as the extension's problem.

### (c) Errors across the boundary; what happens if the extension throws
Every fallible call returns `MongoExtensionStatus*` (`api.h:78-100`): code 0 = OK, `MONGO_EXTENSION_STATUS_RUNTIME_ERROR = -1` for panics/exceptions (`api.h:43`), any other code is passed through (the host rethrows it as an `ExtensionDBException`, `extension_status.h`, `convertStatusToException`). The status is a long-lived object, ownership goes to the caller, who destroys it via its vtable; OK must be a non-null object (a static singleton with a no-op `destroy`, as the SDK does), **NULL is not a valid return** (host would dereference it).
Tested (section D): invalid spec -> code 2 at parse with a reason string; `mode=throw` raises a `std::runtime_error` internally, caught by one `guarded()` wrapper on every vtable function -> host sees code -1 `exception: planted fault...`, and the stage object stays usable; `mode=code` throws a typed error and the chosen code 11601 survives; bad/truncated mesh, missing field, out-of-range index -> code 2 with explanatory reasons; an error **status produced by the host source** while the extension gathers a batch is passed through untouched and destroyed by the host vtable (code 13435 round-trips).
**If an exception escapes**: the SDK marks the vtable functions `noexcept` and the docs say exceptions "MUST NOT cross" (`api_contract.h:86`, `README.md`). My test (`mode=escape`, forked child) terminated with SIGABRT via `std::terminate`: the whole mongod would die. Exceptions inside extension-owned threads must also be caught inside the thread (the `worker` mode captures into `std::exception_ptr` and rethrows on join). Rust panics must likewise be caught (`catch_unwind`) at each entry point.

### (d) Memory ownership of returned buffers
Rule (`public/README.md`, "Memory Ownership"): the allocating side frees through the object's `destroy`; anything transferring ownership has a `destroy` in its vtable. In this extension: `ByteBuf` (outputs, explain, properties, query shape), `Status` errors, parse/ast/logical/exec nodes, the expanded-array container and metrics objects are heap objects destroyed by the host through the vtable; the stage descriptor is extension-owned and never freed; `get_next` documents may be a `kByteBuf` (ownership transferred, `api.h:826-828`) or a `kByteView` that "MUST" stay valid until the next `get_next` on that stage or `destroy` (`api.h:829-832`); ownership of any `kByteBuf` is transferred even when the code is EOF/pause, so the receiver must destroy it (`shared/get_next_result.h`: `destroyTransferredBytes`).
Tested (section C/H): host source x extension output = {buf, view, alt}^2 -> byte-identical output (the fake source poisons its view buffer at the next call, so any retained view would corrupt); counters: **extension-side allocs 606 = frees 606**, **host-side allocs 317 = frees 317** (the extension destroyed all host objects through the host vtable). The first harness run found a real imbalance (1 object): a leak in my own harness (metadata buffer not consumed), which the counters caught; fixed.
ASan+UBSan: one run, clean (81/81, no report). **LeakSanitizer is not supported on macOS arm64** (`detect_leaks is not supported on this platform`), so leak checking relies on the alloc/free counters above. Not tested: allocator mismatch (the production host uses tcmalloc, extensions are built with `--allocator=system` via `bazel/transitions.bzl:75-92`; my harness uses one malloc for both sides).

### (e) `dlopen` another library at runtime; own threads
Works. The extension `dlopen`s a helper dylib lazily (first `get_next`), resolves `mesh_helper_volume` with `dlsym` and uses it for the compute (output byte-identical to the built-in path); it `dlopen`s libSystem and calls `getpid` through `dlsym`; a failing `dlopen` becomes a status (code 3 + `dlerror()` text); it runs a persistent background thread (20+ ticks observed) joined in `destroy`, and computes each batch on a short-lived worker thread (different thread id). The host side loads the extension with `dlopen(RTLD_NOW | RTLD_LOCAL)` (`src/mongo/platform/shared_library_posix.cpp:39`), so extension symbols do not leak into the global namespace; the extension should export only the two entry points (`-fvisibility=hidden` plus `-exported_symbols_list` / `--version-script`, as `test_examples/test_extensions.version_script.lds` does: `global: get_mongodb_extension*; local: *;`). Verified here with `nm -gU` and `dlsym` of an internal symbol.
Warnings from the docs/build: extensions are built with `--allocator=system`, `shared_archive`, `linkstatic`, no PGO (`bazel/transitions.bzl:75-92`); linking `mongo/base` into an extension "could cause issues like host<>extension symbol conflicts at load time or run time" (`README.md`, SERVER-107651). A Vulkan/mesh-library design avoids that by having no `mongo/*` dependency, which this POC confirms is achievable. Threads must not touch `execCtx`, must not let exceptions escape, and the host logger's thread-safety is unverified. Real Vulkan loader (`libvulkan`) not tried here (GPU forbidden for this task).

### (f) Static properties and privileges a stage declares
From `get_properties` (AST node), schema `public/extension_agg_stage_static_properties.idl` (`MongoExtensionStaticProperties`, `strict: false`): `streamType` (`streaming` | `blocking`), `position` (`none|first|last`), `hostType` (`none|receivingHostOnly|collectionlessSourceRunOnceAnyNode|targetedShards|router|allShardHosts`), `requiresInputDocSource` (default true: transform), `requiredPrivileges`, `requiredMetadataFields`, `providedMetadataFields`, `preservesUpstreamMetadata`, `allowedInUnionWith`/`allowedInLookup`/`allowedInFacet` (default true), `isSelectionStage`. `get_docs_needed_bounds` returns a separate `MongoExtensionDocsNeededBoundsInfo` (`effect`: unknown|blocking|possibleDecrease|possibleIncrease|noEffect|limit|skip). This stage declares: streaming, position none, hostType none, requiresInputDocSource true, preservesUpstreamMetadata true, DPL `nullptr`.
Privileges (`public/extension_privilege.idl`): array of `{resourcePattern: "namespace", actions: [{action: <enum>}]}` with a non-empty-actions validator (`extension_privilege_validator.cpp`); the action enum is a closed server-side list (`find`, `listIndexes`, `listSearchIndexes`, `planCacheRead`, `collStats`, `indexStats`, `createSearchIndexes`, `updateSearchIndex`, `dropSearchIndex`; "append-only"). `$meshMeasure` declares `[{resourcePattern: "namespace", actions: [{action: "find"}]}]`. The only resource pattern is the namespace. The harness checked the emitted BSON against the IDL's key names and enums by hand (the IDL parser/validator itself was not run, it needs the server build).
Caveat seen in `test_examples/foo.cpp`: "Right now these constraints are hardcoded to 'kNotAllowed' so we want to test that even if a developer tried to enable them in the extension it wouldn't work" (`allowedInLookup`, with a TODO SERVER-117260 for facet), so subpipeline use may be restricted regardless of what an extension declares. Unverified without a server.

### (g) What the extension API cannot do for us
- **Expression operators** (`$project: {v: {$meshVolume: ...}}`, `$match` predicates such as `$meshIntersects`): no registration path. The portal offers `register_stage_descriptor` and `register_stage_rules` only (`api.h:1284-1297`); the services offer three `create_*` host-stage factories (`api.h:1332-`) for desugaring into host stages (`$_internalSearchIdLookup`, `$_internalDocumentResultsAndMetadata`, a generic host parse node). A mesh feature is a **stage** (`{$meshMeasure: {...}}`) that can only transform or generate documents.
- **Index types / 3D bounding-box index**: nothing in the API. No storage, index or catalog hooks beyond a read-only catalog context at `promote` (namespace, UUID, shard id).
- **New BSON types**: none; meshes must ride in BinData (or arrays/documents).
- **Commands**: `public/extension_command_static_properties.idl` defines `MongoExtensionCommandStaticProperties` (`clusterRole`, `readWriteType`, `allowedOnSecondary`, `adminOnly`, `supportsWriteConcern`, `isSubjectToIngressAdmissionControl`, `requiredPrivileges`), but the only consumer in the tree is its own unit test (`shared/tests/extension_command_static_properties_test.cpp`) and `MongoExtensionHostPortalVTable` has **no command-registration slot**. In this snapshot commands are declared-but-not-registrable (the API is evidently in progress).
- Also: extensions load only at startup (`host/README.md`, "Loading Extensions"), gated by `featureFlagExtensionsAPI` (`src/mongo/db/extension/CLAUDE.md`, "Integration Testing"), signed (GPG) and loaded from a `.conf` under `/etc/mongo/extensions`; API versions MAJOR.MINOR, host supports N and N-1.

## Rust (task 4; nothing was built)

Queried `crates.io/api/v1/crates?q=...` (2026-11-13), repo URLs checked via the `repository` field.

| Crate | Version | License | Kind | Last update |
|---|---|---|---|---|
| `manifold-csg` | 0.4.2 | Apache-2.0 OR MIT | safe **binding** to manifold3d (repo zmerlynn/manifold-csg) | 2026-11-13 |
| `manifold-csg-sys` | 3.5.106 | Apache-2.0 OR MIT | raw FFI **binding** to the manifold3d C API | 2026-11-13 |
| `manifold3d` | 0.4.2 | Apache-2.0 OR MIT | facade crate re-exporting `manifold-csg` (same repo) | 2026-11-13 |
| `manifold3d-sys` | 3.5.106 | Apache-2.0 OR MIT | facade crate re-exporting `manifold-csg-sys` | 2026-11-13 |
| `manifold-rust` | 0.19.2 | Apache-2.0 | **reimplementation** ("Pure Rust port of the Manifold 3D geometry library", repo larsbrubaker/manifold-rust) | 2026-11-01 |
| `manifold3d-types` | 0.0.1 | Apache-2.0 | internal types crate of an older binding (NickUfer/manifold3d-rs); stale | 2024-11-23 |
| `manifold` | 0.1.0 | n/a | unrelated 2D/3D geometry primitives (farshed/manifold) | 2022-08-19 |

The bindings compile the upstream C++ manifold from source (so a Rust extension using them still builds and links C++); `manifold-rust` is a separate port whose parity with upstream (determinism, robustness) is unverified and would need MP03-style testing. I did not read any crate's source or build anything, so API coverage and quality are unverified.
**Extensions API for Rust**: no crate found (queries `mongodb-extension`, `mongodb-extensions`, `mongo-extension`, `mongodb extensions api`, `mongodb-extension-sdk`, `mongot`: only unrelated crates). In this repo: no Rust SDK directory, no `Cargo.toml`, no `.rs` files under `src/mongo/db/extension` or `buildscripts/resmokelib/extensions`; Rust appears only in prose (`README.md`, `CLAUDE.md`, `get_next_result.h:192` comment on `Vec::as_ptr()`). The Search team's production extensions are prebuilt binaries downloaded from S3 (`etc/extensions.yml`: `mongot_extension`, `rerank_extension`, `https://mongot-extension.s3.amazonaws.com/release/...`, only amazon2/amazon2023 variants, `buildscripts/resmokelib/extensions/download_external_extensions.py`). So "Rust vs C++" for us means: hand-written bindings over `api.h` (made harder by the header problems in caveat 2) in either language; the C++ route on the raw ABI is demonstrated here.

## Not done / not verified
- No real `mongod --loadExtensions`; no signature (GPG) check, `.conf` loading, file-ownership/permission checks (`host/signature_validator.cpp`), `featureFlagExtensionsAPI`, sharding/DPL behaviour, views (`bind_resolved_namespace` is a stub), explain through a real pipeline, `$lookup`/`$facet` placement rules.
- Fake host fidelity: written from the headers and from reading `extension_stage.cpp`, the adapters and the handle asserts; it does not reproduce the real host's `ExecAggStage`/`HostExecAggStageAdapter`, operation metrics registry, or interrupt machinery. `get_metrics` is an unsupported stub in the fake host and was never called by the extension.
- Only macOS arm64 / Apple clang 21 was built. Not built: Linux, GCC (the x86_64 GCC argument-evaluation-order issue noted for MP01 is the kind of thing this would catch), `.so` version-script variant (written in `build.sh`, not exercised).
- Allocator mismatch, symbol interposition against a real mongod binary, `RTLD_LOCAL` interactions with tcmalloc, Vulkan loader `dlopen`, driver-thread interaction, and performance (per-document `getOwned` copy cost, BinData size limits) were not measured.
- LeakSanitizer unavailable; leak evidence is the alloc/free counters. No TSan run (threads were exercised but not under TSan).
- Rust crates were only listed; no source or build review.
- The `$meshMeasure` volume is a plain double sum, no exactness; no GPU code of any kind.

## Raw output

### build.sh (symbols, dependencies, headers, both runs)
```
--- exported symbols (production-shaped extension) ---
0000000000000674 T _get_mongodb_extension
0000000000000660 T _get_mongodb_extension_versions
--- dynamic dependencies (production-shaped extension) ---
/Volumes/CrucialX9/Projects/mongo/storage/mesh/extension/mesh_extension_prod.dylib:
	/Volumes/CrucialX9/Projects/mongo/storage/mesh/extension/mesh_extension_prod.dylib (compatibility version 0.0.0, current version 0.0.0)
	/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 2200.27.0)
	/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1359.0.0)
--- headers included by the extension (clang -M, filtered) ---
/Volumes/CrucialX9/Projects/mongo/storage/mesh/extension/include/mongo/db/extension/public/api.h
/Volumes/CrucialX9/Projects/mongo/storage/mesh/extension/include/mongo/util/modules.h
/Volumes/CrucialX9/Projects/mongo/storage/mesh/extension/include/mongo/db/extension/public/api_contract.h
/Volumes/CrucialX9/Projects/mongo/poc/gpu/mesh/extension/mesh_format.h
/Volumes/CrucialX9/Projects/mongo/poc/gpu/mesh/extension/mini_bson.h
--- run: plain ---
== A. load, negotiate, register ==
  [PASS] dlopen(RTLD_NOW|RTLD_LOCAL) ok
  [PASS] dlsym get_mongodb_extension_versions / get_mongodb_extension
  [PASS] internal symbols are not visible to dlsym
  extension advertises 1 version(s): 1.0
  [PASS] negotiated version 1.0 (major must match, extension minor <= host minor)
  [PASS] Phase 2 with unadvertised 2.0 -> code 2 'unsupported API version 2.0'
  [PASS] get_mongodb_extension(1.0) code=0
  [PASS] extension->version == negotiated (host uassert 13693501)
  [PASS] initialize() code=0 
  [PASS] exactly one stage descriptor registered (got 1)
  [PASS] descriptor name '$meshMeasure'
  [PASS] client type = Any (user pipelines may use it)

== B. parse -> expand -> promote -> compile, static properties, vtable audit ==
  [PASS] build ok ( 0 )
  [PASS] query shape routed 'field' through host serialize_field_path -> 'HASH<mesh>'
  [PASS] streamType 'streaming' valid enum
  [PASS] position none
  [PASS] hostType none
  [PASS] all property keys known to the IDL: streamType position hostType requiresInputDocSource requiredPrivileges preservesUpstreamMetadata allowedInUnionWith allowedInLookup allowedInFacet isSelectionStage 
  [PASS] requiredPrivileges[0].actions[0].action = 'find' (extension_privilege.idl enum)
  [PASS] LogicalAggStage.serialize round-trips the stage definition
  [PASS] serialized form re-parses (what a shard receives from mongos)
  [PASS] LogicalAggStage.explain returns BSON
  [PASS] get_distributed_plan_logic -> nullptr (runs fully on shards)
  [PASS] logical clone
  [PASS] parse clone
  [PASS] ast clone
  [PASS] to_bson_for_log
  [PASS] no rewrite rules fire
  [PASS] docsNeededBounds effect noEffect
  [PASS] metrics object works
  [PASS] invalid spec rejected at parse: code=2 '$meshMeasure: bad mode 'nonsense''
  [PASS] status clone keeps the code
  vtable slots audited: 50, NULL: 0

== C. batching: one document per get_next, N pulled from the source per dispatch ==
  [PASS] 20 docs out, order kept, volume == side^3 within 1e-9 rel, other fields preserved
  [PASS] get_next calls = 21 (20 docs + EOF): exactly ONE document per call
  [PASS] first output only after 8 source pulls (batched gather): srcCalls@first=8
  [PASS] upstream metadata container passed through for docs 0,3,6,...
  [PASS] explain: dispatches=3 maxBatch=8 sourcePulls=21 (expect 3/8/21)
  [PASS] host source=buf x extension out=buf -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=buf x extension out=view -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=buf x extension out=alt -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=view x extension out=buf -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=view x extension out=view -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=view x extension out=alt -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=alt x extension out=buf -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=alt x extension out=view -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=alt x extension out=alt -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  batch=1    -> dispatches=20 maxBatch=1, outputs=20
  [PASS] batch=1 yields all 20 docs
  batch=5    -> dispatches=4 maxBatch=5, outputs=20
  [PASS] batch=5 yields all 20 docs
  batch=20   -> dispatches=1 maxBatch=20, outputs=20
  [PASS] batch=20 yields all 20 docs
  batch=1000 -> dispatches=1 maxBatch=20, outputs=20
  [PASS] batch=1000 yields all 20 docs
  [PASS] portal option 'defaultBatch: 4' honoured: maxBatch=4 dispatches=5
  [PASS] ctx->set_batch_size -> 13150705 'setBatchSize is not supported outside of a document results and metadata stream' (mirrors query_execution_context_adapter.cpp:66-70)

== D. errors across the boundary ==
  [PASS] parse error 'bogus=1' -> code 2 reason '$meshMeasure: unknown or mistyped field 'bogus''
  [PASS] parse error 'mode=sideways' -> code 2 reason '$meshMeasure: bad mode 'sideways''
  [PASS] parse error 'batch=0' -> code 2 reason 'batch must be in [1, 1e6]'
  [PASS] parse error 'out=file' -> code 2 reason '$meshMeasure: bad out 'file''
  [PASS] wrong stage name -> code 2 'unexpected stage name $other'
  [PASS] empty argument object accepted (defaults)
  [PASS] C++ exception caught at boundary -> status code -1 (MONGO_EXTENSION_STATUS_RUNTIME_ERROR) 'exception: planted fault inside $meshMeasure (mode=throw)'
  [PASS] stage object still callable after a caught exception (second call also reports the status)
  [PASS] extension-chosen error code preserved: 11601 'planted uassert-style error (mode=code)'
  [PASS] truncated mesh -> code 2 'invalid mesh payload: mesh payload size 339 != expected 344' (whole batch fails: no partial output, 0 docs out)
  [PASS] missing field -> code 2 'document has no field 'mesh''
  [PASS] triangle index out of range -> code 2 'invalid mesh payload: triangle index out of range'
  [PASS] host status passed through: code 13435 'injected host source failure' (destroyed via the host's own vtable)
libc++abi: terminating due to uncaught exception of type std::runtime_error: escaping exception (mode=escape)
  [PASS] exception escaping the extension's C entry point kills the process (signal 6 Abort trap: 6): the host cannot recover

== E. deadlines, killOp, no preemption ==
  [PASS] deadline in the past -> per-batch check_for_interrupt surfaces code 50 'operation exceeded time limit'
  [PASS] no deadline (INT64_MAX) -> runs normally
  [PASS] non-polling 2 s batch: returned code 0 after 2005 ms, ignoring the 300 ms deadline (host cannot preempt)
  [PASS] ...host only notices afterwards: its next check_for_interrupt says 50 'operation exceeded time limit'
  [PASS] polling 2 s batch: code 50 after 322 ms (deadline was +300 ms) 'operation exceeded time limit'
  [PASS] killOp at +200 ms -> code 11601 after 225 ms 'operation was interrupted'
  note: execCtx is valid only for the duration of one get_next (not retained by this extension)

== F. dlopen from inside the extension, own threads ==
  [PASS] extension dlopen()ed /Volumes/CrucialX9/Projects/mongo/storage/mesh/extension/libmeshhelper.dylib and called mesh_helper_volume via dlsym
  [PASS] extension dlopen(libSystem/libc)+dlsym(getpid) -> 'dlopen+dlsym ok'
  [PASS] batch computed on an extension-owned worker thread (different thread id)
  [PASS] persistent extension thread ticking: 20 ticks
  [PASS] results byte-identical to the in-extension compute path
  [PASS] failed dlopen -> status code 3 'dlopen failed: dlopen(/nonexistent/libnope.dylib, 0x0006): tried: '/nonexistent/libnope.dylib' (no such file), '/System/Volumes/Preboot/Cryptexes/OS/nonexistent/libnope.dylib' (no such file), '/nonexistent/libnope.dylib' (no such file)'

== G. lifecycle: open/reopen/close (the real host never calls them; see REPORT) ==
  [PASS] open -> run -> reopen -> run -> close works (6 + 6 docs)
  [PASS] open/close counted by the extension

== H. memory ownership: allocation balance ==
  [PASS] extension-side heap objects handed across the boundary: allocs=606 frees=606 (every one destroyed by the host through the vtable)
  [PASS] host-side statuses/buffers given to the extension: allocs=317 frees=317 (extension destroyed all through the host vtable)

== vtable entries the harness called (type.slot: count) ==
  AstNode.clone                                                            1
  AstNode.destroy                                                          32
  AstNode.get_properties                                                   32
  AstNode.promote                                                          32
  Descriptor.get_client_type                                               1
  Descriptor.get_name                                                      1
  Descriptor.parse                                                         39
  ExecAggStage.close                                                       1
  ExecAggStage.create_metrics                                              1
  ExecAggStage.destroy                                                     32
  ExecAggStage.explain                                                     8
  ExecAggStage.get_next                                                    367
  ExecAggStage.open                                                        1
  ExecAggStage.reopen                                                      1
  ExecAggStage.set_source                                                  31
  ExpandedArrayContainer.destroy                                           32
  ExpandedArrayContainer.size                                              32
  ExpandedArrayContainer.transfer                                          32
  LogicalAggStage.clone                                                    1
  LogicalAggStage.compile                                                  32
  LogicalAggStage.destroy                                                  32
  LogicalAggStage.evaluate_pipeline_rewrite_rule_precondition              1
  LogicalAggStage.explain                                                  1
  LogicalAggStage.get_distributed_plan_logic                               1
  LogicalAggStage.get_docs_needed_bounds                                   1
  LogicalAggStage.serialize                                                1
  MongoExtension.initialize                                                1
  OperationMetrics.destroy                                                 1
  OperationMetrics.serialize                                               1
  OperationMetrics.update                                                  1
  ParseNode.clone                                                          1
  ParseNode.destroy                                                        32
  ParseNode.expand                                                         32
  ParseNode.get_query_shape                                                32
  ParseNode.to_bson_for_log                                                1
  QueryExecutionContext.set_batch_size                                     1

== host log lines from extension: 0 ==

RESULT: 81 passed, 0 failed
--- run: ASan+UBSan (detect_leaks=0) ---
== A. load, negotiate, register ==
  [PASS] dlopen(RTLD_NOW|RTLD_LOCAL) ok
  [PASS] dlsym get_mongodb_extension_versions / get_mongodb_extension
  [PASS] internal symbols are not visible to dlsym
  extension advertises 1 version(s): 1.0
  [PASS] negotiated version 1.0 (major must match, extension minor <= host minor)
  [PASS] Phase 2 with unadvertised 2.0 -> code 2 'unsupported API version 2.0'
  [PASS] get_mongodb_extension(1.0) code=0
  [PASS] extension->version == negotiated (host uassert 13693501)
  [PASS] initialize() code=0 
  [PASS] exactly one stage descriptor registered (got 1)
  [PASS] descriptor name '$meshMeasure'
  [PASS] client type = Any (user pipelines may use it)

== B. parse -> expand -> promote -> compile, static properties, vtable audit ==
  [PASS] build ok ( 0 )
  [PASS] query shape routed 'field' through host serialize_field_path -> 'HASH<mesh>'
  [PASS] streamType 'streaming' valid enum
  [PASS] position none
  [PASS] hostType none
  [PASS] all property keys known to the IDL: streamType position hostType requiresInputDocSource requiredPrivileges preservesUpstreamMetadata allowedInUnionWith allowedInLookup allowedInFacet isSelectionStage 
  [PASS] requiredPrivileges[0].actions[0].action = 'find' (extension_privilege.idl enum)
  [PASS] LogicalAggStage.serialize round-trips the stage definition
  [PASS] serialized form re-parses (what a shard receives from mongos)
  [PASS] LogicalAggStage.explain returns BSON
  [PASS] get_distributed_plan_logic -> nullptr (runs fully on shards)
  [PASS] logical clone
  [PASS] parse clone
  [PASS] ast clone
  [PASS] to_bson_for_log
  [PASS] no rewrite rules fire
  [PASS] docsNeededBounds effect noEffect
  [PASS] metrics object works
  [PASS] invalid spec rejected at parse: code=2 '$meshMeasure: bad mode 'nonsense''
  [PASS] status clone keeps the code
  vtable slots audited: 50, NULL: 0

== C. batching: one document per get_next, N pulled from the source per dispatch ==
  [PASS] 20 docs out, order kept, volume == side^3 within 1e-9 rel, other fields preserved
  [PASS] get_next calls = 21 (20 docs + EOF): exactly ONE document per call
  [PASS] first output only after 8 source pulls (batched gather): srcCalls@first=8
  [PASS] upstream metadata container passed through for docs 0,3,6,...
  [PASS] explain: dispatches=3 maxBatch=8 sourcePulls=21 (expect 3/8/21)
  [PASS] host source=buf x extension out=buf -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=buf x extension out=view -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=buf x extension out=alt -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=view x extension out=buf -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=view x extension out=view -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=view x extension out=alt -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=alt x extension out=buf -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=alt x extension out=view -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  [PASS] host source=alt x extension out=alt -> byte-identical output (kByteView/kByteBuf lifetimes honoured)
  batch=1    -> dispatches=20 maxBatch=1, outputs=20
  [PASS] batch=1 yields all 20 docs
  batch=5    -> dispatches=4 maxBatch=5, outputs=20
  [PASS] batch=5 yields all 20 docs
  batch=20   -> dispatches=1 maxBatch=20, outputs=20
  [PASS] batch=20 yields all 20 docs
  batch=1000 -> dispatches=1 maxBatch=20, outputs=20
  [PASS] batch=1000 yields all 20 docs
  [PASS] portal option 'defaultBatch: 4' honoured: maxBatch=4 dispatches=5
  [PASS] ctx->set_batch_size -> 13150705 'setBatchSize is not supported outside of a document results and metadata stream' (mirrors query_execution_context_adapter.cpp:66-70)

== D. errors across the boundary ==
  [PASS] parse error 'bogus=1' -> code 2 reason '$meshMeasure: unknown or mistyped field 'bogus''
  [PASS] parse error 'mode=sideways' -> code 2 reason '$meshMeasure: bad mode 'sideways''
  [PASS] parse error 'batch=0' -> code 2 reason 'batch must be in [1, 1e6]'
  [PASS] parse error 'out=file' -> code 2 reason '$meshMeasure: bad out 'file''
  [PASS] wrong stage name -> code 2 'unexpected stage name $other'
  [PASS] empty argument object accepted (defaults)
  [PASS] C++ exception caught at boundary -> status code -1 (MONGO_EXTENSION_STATUS_RUNTIME_ERROR) 'exception: planted fault inside $meshMeasure (mode=throw)'
  [PASS] stage object still callable after a caught exception (second call also reports the status)
  [PASS] extension-chosen error code preserved: 11601 'planted uassert-style error (mode=code)'
  [PASS] truncated mesh -> code 2 'invalid mesh payload: mesh payload size 339 != expected 344' (whole batch fails: no partial output, 0 docs out)
  [PASS] missing field -> code 2 'document has no field 'mesh''
  [PASS] triangle index out of range -> code 2 'invalid mesh payload: triangle index out of range'
  [PASS] host status passed through: code 13435 'injected host source failure' (destroyed via the host's own vtable)
libc++abi: terminating due to uncaught exception of type std::runtime_error: escaping exception (mode=escape)
  [PASS] exception escaping the extension's C entry point kills the process (signal 6 Abort trap: 6): the host cannot recover

== E. deadlines, killOp, no preemption ==
  [PASS] deadline in the past -> per-batch check_for_interrupt surfaces code 50 'operation exceeded time limit'
  [PASS] no deadline (INT64_MAX) -> runs normally
  [PASS] non-polling 2 s batch: returned code 0 after 2005 ms, ignoring the 300 ms deadline (host cannot preempt)
  [PASS] ...host only notices afterwards: its next check_for_interrupt says 50 'operation exceeded time limit'
  [PASS] polling 2 s batch: code 50 after 318 ms (deadline was +300 ms) 'operation exceeded time limit'
  [PASS] killOp at +200 ms -> code 11601 after 223 ms 'operation was interrupted'
  note: execCtx is valid only for the duration of one get_next (not retained by this extension)

== F. dlopen from inside the extension, own threads ==
  [PASS] extension dlopen()ed /Volumes/CrucialX9/Projects/mongo/storage/mesh/extension/asan_libmeshhelper.dylib and called mesh_helper_volume via dlsym
  [PASS] extension dlopen(libSystem/libc)+dlsym(getpid) -> 'dlopen+dlsym ok'
  [PASS] batch computed on an extension-owned worker thread (different thread id)
  [PASS] persistent extension thread ticking: 20 ticks
  [PASS] results byte-identical to the in-extension compute path
  [PASS] failed dlopen -> status code 3 'dlopen failed: dlopen(/nonexistent/libnope.dylib, 0x0006): tried: '/nonexistent/libnope.dylib' (no such file), '/System/Volumes/Preboot/Cryptexes/OS/nonexistent/libnope.dylib' (no such file), '/nonexistent/libnope.dylib' (no such file)'

== G. lifecycle: open/reopen/close (the real host never calls them; see REPORT) ==
  [PASS] open -> run -> reopen -> run -> close works (6 + 6 docs)
  [PASS] open/close counted by the extension

== H. memory ownership: allocation balance ==
  [PASS] extension-side heap objects handed across the boundary: allocs=606 frees=606 (every one destroyed by the host through the vtable)
  [PASS] host-side statuses/buffers given to the extension: allocs=317 frees=317 (extension destroyed all through the host vtable)

== vtable entries the harness called (type.slot: count) ==
  AstNode.clone                                                            1
  AstNode.destroy                                                          32
  AstNode.get_properties                                                   32
  AstNode.promote                                                          32
  Descriptor.get_client_type                                               1
  Descriptor.get_name                                                      1
  Descriptor.parse                                                         39
  ExecAggStage.close                                                       1
  ExecAggStage.create_metrics                                              1
  ExecAggStage.destroy                                                     32
  ExecAggStage.explain                                                     8
  ExecAggStage.get_next                                                    367
  ExecAggStage.open                                                        1
  ExecAggStage.reopen                                                      1
  ExecAggStage.set_source                                                  31
  ExpandedArrayContainer.destroy                                           32
  ExpandedArrayContainer.size                                              32
  ExpandedArrayContainer.transfer                                          32
  LogicalAggStage.clone                                                    1
  LogicalAggStage.compile                                                  32
  LogicalAggStage.destroy                                                  32
  LogicalAggStage.evaluate_pipeline_rewrite_rule_precondition              1
  LogicalAggStage.explain                                                  1
  LogicalAggStage.get_distributed_plan_logic                               1
  LogicalAggStage.get_docs_needed_bounds                                   1
  LogicalAggStage.serialize                                                1
  MongoExtension.initialize                                                1
  OperationMetrics.destroy                                                 1
  OperationMetrics.serialize                                               1
  OperationMetrics.update                                                  1
  ParseNode.clone                                                          1
  ParseNode.destroy                                                        32
  ParseNode.expand                                                         32
  ParseNode.get_query_shape                                                32
  ParseNode.to_bson_for_log                                                1
  QueryExecutionContext.set_batch_size                                     1

== host log lines from extension: 0 ==

RESULT: 81 passed, 0 failed
```
