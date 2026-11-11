# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This is the MongoDB server source (`mongod`, `mongos`). Build system is **Bazel** (not SCons); C++20; Python 3.13 for tooling (`buildscripts/`), with a `uv`-managed venv.

## Build

```bash
python buildscripts/install_bazel.py        # one-time; then ensure ~/.local/bin is on PATH
bazel build install-mongod                  # just mongod
bazel build install-devcore                 # mongod + mongos + jstestshell (use for testing)
bazel build install-dist-test               # everything resmoke suites need
bazel build --build_tag_filters=mongo_binary //src/mongo/...   # absolutely everything
```

- Binaries land in `bazel-bin/install/bin/`.
- Warnings are errors; with a newer-than-supported compiler add `--disable_warnings_as_errors=True`.
- Debug build: `--config=dbg`. Local overrides go in `.bazelrc.local` (see `.bazelrc.local.example`).
- Details: `docs/building.md`.

## Tests

**C++ unit tests** are `mongo_cc_unit_test` Bazel targets (GoogleTest underneath, include `mongo/unittest/unittest.h`; never include gmock directly; use `DEATH_TEST` not `ASSERT_DEATH`). Targets are named per BUILD file, e.g.:

```bash
bazel test //src/mongo/bson:bson_test
bazel build --config=dbg +bson_test         # `+name` is a shorthand for a target by name
```

Other C++ test kinds: `mongo_cc_integration_test`, `mongo_cc_benchmark`, fuzztests (`docs/fuzztest.md`, `--config=fuzztest`). See `docs/unit_test.md`.

**JS integration tests** (`jstests/`) run through **resmoke** against built binaries. Activate the venv first (`python3 -m venv python3-venv && source python3-venv/bin/activate && buildscripts/uv_sync.sh`), then:

```bash
buildscripts/resmoke.py run --suites=no_passthrough jstests/noPassthrough/shell/js/string.js
buildscripts/resmoke.py list-suites
```

A test is only run by a *suite* (`buildscripts/resmokeconfig/suites/*.yml`) that selects it by glob/tags; a suite picks a fixture (topology: standalone, replset, sharded…) plus hooks. Many `jstests/` dirs (`core`, `aggregation`, …) are run via many "passthrough" suites with different fixtures, so a test must be compatible with all of them or be excluded with tags (e.g. `requires_sharding`, `requires_fcv_*`). Docs: `docs/testing/README.md`, `buildscripts/resmokelib/README.md`, `buildscripts/resmokeconfig/suites/README.md`.

`jstests/AGENTS.md` (imported by `jstests/CLAUDE.md`) holds JS test conventions: use `assert.commandWorked` etc. from `src/mongo/shell/assert.js`, mocha-style (`jstests/libs/mochalite.js`) for new files, pass objects via `attr` rather than `tojson()` concatenation, always clean up resources/settings.

## Lint / format

```bash
bazel run checks                            # checks files changed on this branch (no modification)
bazel run checks -- --fix --group format --group lint
bazel run checks -- --list                  # stable check IDs; use --only/--skip/--files
```

`bazel run format`, `bazel run lint`, `bazel run codeowners` still work. Extra C++ linters: `buildscripts/clang_tidy.py` (needs compile_commands.json), `buildscripts/errorcodes.py` (assertion codes must be unique), `buildscripts/quickmongolint.py`. See `docs/linting.md`. C++ style guide: `docs/cpp_style.md` (types `TitleCase`, functions/variables `camelCase`).

## Architecture (big picture)

- `src/mongo/db/` — the server proper. Major areas are subdirectories with their own READMEs worth reading before touching them: `repl` (replication), `query`/`pipeline`/`exec`/`matcher` (query & aggregation), `storage` (WiredTiger integration; `src/third_party/wiredtiger` has its own `AGENTS.md`), `catalog`/`shard_role`/`index_builds`, `auth`, `timeseries`, `ttl`, `views`, `change_stream*`, `op_observer`, `transaction`.
- `src/mongo/s/` — sharding: router (`mongos`) logic, plus shard-side sharding components under `db/s`/`db/sharding_environment`. Request handling differs by role: `ServiceEntryPointRouterRole` vs `ServiceEntryPointShardRole` (see `docs/command_dispatch.md`).
- `src/mongo/transport`, `rpc`, `executor`, `client` — networking, wire protocol, task executors, client library. `src/mongo/util`, `base`, `bson`, `logv2`, `platform`, `stdx` — foundations (use `stdx` wrappers, `logv2` for logging; see `docs/logging.md`, `docs/exception_architecture.md`, `docs/memory_management.md`).
- `src/mongo/shell` — the JS test shell (`jstestshell`) and its JS libs; `src/mongo/scripting` — embedded JS engine.
- `src/mongo/idl` — **IDL** (`.idl` files) is the code generator for BSON-parsing structs, commands, enums, and server parameters. Edit the `.idl` and the generated C++ follows via Bazel (`idl_generator`/`mongo_idl_library`); don't hand-write command parsing. Docs: `docs/idl.md`, `docs/server_parameters.md`.
- Commands are registered at startup in a global `CommandRegistry` (`BasicCommand` / `TypedCommand`); failpoints (`docs/fail_points.md`) are the main hook for tests to inject behavior.
- **Module boundaries**: `modules_poc/modules.yaml` assigns every file to exactly one module, and visibility macros in `src/mongo/util/modules.h` (`PUBLIC`, `PRIVATE`, `FILE_PRIVATE`, …) mark API surface. Respect them when adding cross-module includes (`docs/modularity.md`).
- Bazel rules live in `bazel/` (`mongo_src_rules.bzl` defines `mongo_cc_library`, `mongo_cc_unit_test`, etc.); each source dir has a `BUILD.bazel` — new `.cpp`/`.h` files must be added to its `srcs`/`hdrs`.
- `buildscripts/` — resmoke (`resmokelib/`, `resmokeconfig/`), linters, packager, and CI helpers; `evergreen/` and `etc/evergreen*.yml` — CI config.

## Repo conventions

- Commit messages start with the Jira ticket: `SERVER-12345 Short description (#PR)`.
- `master` is auto-synced to the public `mongodb/mongo` repo, so a change that looks like a security/CVE fix must not land before public disclosure (see `.github/copilot-instructions.md`).
- Code ownership is tracked via `OWNERS.yml` files (checked by `bazel run codeowners`).
