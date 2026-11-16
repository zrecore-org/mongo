#!/bin/bash
# MP14d on the real fork mongod, CPU path: starts mongod with the extension (extensionOptions per case), runs test_mp14d.py, stops it. Usage: run_mp14d_real.sh <mongod> <so> <real60 dir>
set -u
ulimit -c 0
H="$(cd "$(dirname "$0")" && pwd)"; MONGOD="$1"; SO="$2"; D="$3"; PY="${PY:-$H/venv/bin/python}"; FAILS=0
want() { [ -z "${CASES:-}" ] || echo " $CASES " | grep -q " $1 "; }
case_run() {  # name, extensionOptions block, env assignments for the test (e.g. "EXPECT_GPU=1 MP14D_GPU_MIN=1")
  local name="$1" opts="$2" envs="${3:-}" R="$H/run_$1"; rm -rf "$R"; mkdir -p "$R/db" "$R/conf"; chmod 755 "$R/conf"
  { printf 'sharedLibraryPath: %s\n' "$SO"; [ -n "$opts" ] && printf '%s\n' "$opts"; } > "$R/conf/meshvol_ext.conf"; chmod 644 "$R/conf/meshvol_ext.conf"
  cat > "$R/mongod.conf" <<EOT
processManagement: {loadExtensions: [meshvol_ext], extensionsConfigPath: $R/conf, pidFilePath: $R/pid}
setParameter: {featureFlagExtensionsAPI: true}
net: {port: 27555, bindIp: 127.0.0.1}
storage: {dbPath: $R/db}
systemLog: {destination: file, path: $R/mongod.log}
EOT
  echo "== case $name; extensionOptions: [$(echo "$opts" | tr '\n' '|')]"
  nice -n 10 "$MONGOD" -f "$R/mongod.conf" > "$R/start.out" 2>&1 &
  local MP=$!; local up=no
  for i in $(seq 1 90); do grep -q "Waiting for connections" "$R/mongod.log" 2>/dev/null && { up=yes; break; }; kill -0 $MP 2>/dev/null || break; sleep 1; done
  if [ "$up" = yes ]; then
    grep -c "libvulkan" /proc/$MP/maps | sed 's/^/vulkan-library-mappings: /'
    grep "meshvolume: gpu" "$R/mongod.log" | cut -c1-300 | head -2
    env $envs timeout 1500 "$PY" "$H/test_mp14d.py" 27555 "$D" 2>&1 | tee "$R/test.out"
    grep -q "^FAILS 0" "$R/test.out" && echo "PASS $name: test_mp14d FAILS 0" || { echo "FAIL $name: test_mp14d"; FAILS=$((FAILS+1)); }
  else echo "FAIL $name: server did not start"; tail -3 "$R/start.out" | cut -c1-400; FAILS=$((FAILS+1)); fi
  [ "$up" = yes ] && { grep -c "dzn\|libvulkan_" /proc/$MP/maps | sed 's/^/ICD-library-mappings-at-end: /'; }
  kill $MP 2>/dev/null; wait $MP 2>/dev/null; echo "$?" > "$R/exit.rc"; sleep 1
  # a crash in a driver thread can be hidden for tens of seconds behind a slow backtrace: look for the fatal signal explicitly
  local sig=$(grep -c 'Got signal\|Invalid access at address\|abruptQuit' "$R/mongod.log" 2>/dev/null)
  [ "$sig" = 0 ] && echo "PASS $name: no fatal signal in the server log" || { echo "FAIL $name: $sig fatal-signal lines in the server log"; grep -m3 'Got signal\|Invalid access' "$R/mongod.log" | cut -c1-240; FAILS=$((FAILS+1)); }
}
want cpu_default && case_run cpu_default ""
want gpu_lavapipe && case_run gpu_lavapipe $'extensionOptions:\n  gpu: true' "EXPECT_GPU=0 MP14D_GPU_MIN=1"
sigterm_case() {  # SIGTERM while device batches are in flight: clean exit rc 0 and no backtrace
  local name="$1" opts="$2" R="$H/run_$1"; rm -rf "$R"; mkdir -p "$R/db" "$R/conf"; chmod 755 "$R/conf"
  { printf 'sharedLibraryPath: %s\n' "$SO"; printf '%s\n' "$opts"; } > "$R/conf/meshvol_ext.conf"; chmod 644 "$R/conf/meshvol_ext.conf"
  cat > "$R/mongod.conf" <<EOT
processManagement: {loadExtensions: [meshvol_ext], extensionsConfigPath: $R/conf, pidFilePath: $R/pid}
setParameter: {featureFlagExtensionsAPI: true}
net: {port: 27555, bindIp: 127.0.0.1}
storage: {dbPath: $R/db}
systemLog: {destination: file, path: $R/mongod.log}
EOT
  echo "== case $name (SIGTERM while device batches are in flight)"
  nice -n 10 "$MONGOD" -f "$R/mongod.conf" > "$R/start.out" 2>&1 &
  local MP=$!; local up=no
  for i in $(seq 1 90); do grep -q "Waiting for connections" "$R/mongod.log" 2>/dev/null && { up=yes; break; }; kill -0 $MP 2>/dev/null || break; sleep 1; done
  if [ "$up" = yes ]; then
    "$PY" "$H/load_loop.py" 27555 "$D" 120 "$R/progress.txt" > "$R/loop.out" 2>&1 &
    local LP=$!
    for i in $(seq 1 60); do grep -q "LOOP STARTED" "$R/loop.out" 2>/dev/null && break; sleep 1; done
    sleep 6
    "$PY" "$H/counters.py" 27555 > "$R/counters_before.txt" 2>&1; echo "counters before SIGTERM: $(cat "$R/counters_before.txt" | grep -o "'gpuBatches': [0-9]*")"
    local tsig=$(date +%s.%N 2>/dev/null || date +%s); kill -TERM $MP; local t0=$(date +%s); wait $MP; local rc=$?; local dt=$(( $(date +%s) - t0 ))
    for i in $(seq 1 15); do grep -q "LOOP ENDED\|LOOP FINISHED" "$R/loop.out" 2>/dev/null && break; sleep 1; done
    kill $LP 2>/dev/null; wait $LP 2>/dev/null
    echo "$rc" > "$R/exit.rc"; tail -2 "$R/loop.out"; echo "progress lines: $(wc -l < "$R/progress.txt")  last: $(tail -1 "$R/progress.txt")"
    local nb=$(grep -o "'gpuBatches': [0-9]*" "$R/counters_before.txt" | grep -o "[0-9]*$")
    [ "${nb:-0}" -gt 0 ] && echo "PASS $name: device batches ran before the signal (gpuBatches=$nb)" || { echo "FAIL $name: no device batch before the signal"; FAILS=$((FAILS+1)); }
    local last=$(tail -2 "$R/progress.txt" | head -1)
    grep -q "^ended" "$R/progress.txt" && echo "PASS $name: the loop was cut by the shutdown (it was running: $last)" || { echo "FAIL $name: the loop was not interrupted by the shutdown"; FAILS=$((FAILS+1)); }
    [ "$(grep -c '^start' "$R/progress.txt")" -gt "$(grep -c '^done' "$R/progress.txt")" ] && echo "PASS $name: an aggregation was in flight when the connection dropped" || echo "note $name: no aggregation was mid-flight at the drop (the shutdown fell between two aggregations)"
    [ "$rc" = 0 ] && echo "PASS $name: clean exit rc 0 after SIGTERM in ${dt}s" || { echo "FAIL $name: exit rc $rc"; FAILS=$((FAILS+1)); }
    [ "$(grep -c 'abruptQuit\|BACKTRACE\|Got signal\|Invalid access' "$R/mongod.log")" = 0 ] && echo "PASS $name: no fatal signal or backtrace in the log" || { echo "FAIL $name: fatal signal or backtrace in the log"; FAILS=$((FAILS+1)); }
  else echo "FAIL $name: server did not start"; FAILS=$((FAILS+1)); fi
}
want gpu_dzn_sigterm && sigterm_case gpu_dzn_sigterm $'extensionOptions:\n  gpu: true'
want gpu_dzn && case_run gpu_dzn $'extensionOptions:\n  gpu: true' "EXPECT_GPU=1 MP14D_GPU_MIN=1"
echo "FAILS $FAILS"
