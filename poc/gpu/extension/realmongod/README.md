# Real-`mongod` test jobs (MP14b/c/d)

Scripts that start the fork-built `mongod` (Linux, non-release) with `meshvol_ext` and test it through real aggregations. They ran in an Ubuntu 24.04 x86_64 container on the shared Windows machine
(CPU cases: plain container; device cases: `--device /dev/dxg`, `/usr/lib/wsl`, a private Mesa Dozen build; see exactmesh `docs/RESULTS.md`). Package layout used there: `g5_bundle/` (extension sources + `build_linux.sh`),
`mp14d_pkg/` (these scripts + `real60/`, the 60-model documents written by `real_contains <list> <threads> <dir>`).
- `run_mp14c_real.sh` MP14c option gate cases; `test_g5.py` the G5 suite.
- `run_mp14d_real.sh` MP14d cases (`CASES="cpu_default gpu_lavapipe gpu_dzn gpu_dzn_sigterm"`), `test_mp14d.py`, `load_loop.py`, `counters.py`.
- `mp14_cpu_container_job.sh`, `mp14_dzn_container_job.sh` the container entry scripts (file names inside refer to the share's tarballs).
Rules learned the hard way: `ulimit -c 0` and `--ulimit core=0` do NOT stop WSL2 core dumps (the WSL `core_pattern` is a pipe handler that ignores a limit of 0; run containers with `--ulimit core=1:1` and use `prlimit --core=1:1 --pid $$`, never `ulimit -c 0`; see `../DEV_LOADING.md`); mongod's crash handler can take about 30 s to finish its backtrace while the server keeps serving, so every run greps the log for `Got signal|Invalid access|abruptQuit`; run directories belong on a root-owned container volume, not a Windows mount.
