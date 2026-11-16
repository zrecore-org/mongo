#!/bin/bash
# MP14c, real fork mongod, CPU only. Usage: ./run_mp14c_real.sh <mongod> <libmeshvol_ext.so>. Cases: extensionOptions absent / {} / gpu false (must start, requested=false, no Vulkan library mapped),
# gpu true (software device on lavapipe: starts, ready=false), gpu true + gpuRequired true (must REFUSE to start, reason logged), unknown option (must refuse). Port 27555, 127.0.0.1 only.
set -u
ulimit -c 0   # a refused load is a fatal assertion (SIGABRT); without this WSL writes a ~2 GB core dump per refusal to C:\
H="$(cd "$(dirname "$0")" && pwd)"; MONGOD="$1"; SO="$2"; FAILS=0
chk() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1"; FAILS=$((FAILS+1)); fi; }
yes_if() { grep -q -- "$2" "$1" 2>/dev/null && echo ok || echo no; }
want() { [ -z "${CASES:-}" ] || echo " $CASES " | grep -q " $1 "; }   # CASES="gpu_true gpu_required" runs only those cases
one() {  # name, extensionOptions block (may be empty), expect: start|refuse
  local name="$1" opts="$2" expect="$3" R="$H/run_$1"; rm -rf "$R"; mkdir -p "$R/db" "$R/conf"; chmod 755 "$R/conf"
  { printf 'sharedLibraryPath: %s\n' "$SO"; [ -n "$opts" ] && printf '%s\n' "$opts"; } > "$R/conf/meshvol_ext.conf"; chmod 644 "$R/conf/meshvol_ext.conf"
  cat > "$R/mongod.conf" <<EOT
processManagement: {loadExtensions: [meshvol_ext], extensionsConfigPath: $R/conf, pidFilePath: $R/pid}
setParameter: {featureFlagExtensionsAPI: true}
net: {port: 27555, bindIp: 127.0.0.1}
storage: {dbPath: $R/db}
systemLog: {destination: file, path: $R/mongod.log}
EOT
  echo "== case $name (expect $expect); conf extensionOptions: [$(echo "$opts" | tr '\n' '|')]"
  nice -n 10 "$MONGOD" -f "$R/mongod.conf" > "$R/start.out" 2>&1 &
  local MP=$!; local up=no
  for i in $(seq 1 60); do grep -q "Waiting for connections" "$R/mongod.log" 2>/dev/null && { up=yes; break; }; kill -0 $MP 2>/dev/null || break; sleep 1; done
  if [ "$up" = yes ]; then
    grep -c "libvulkan" /proc/$MP/maps | sed 's/^/vulkan-library-mappings: /' | tee "$R/maps.count"
  fi
  grep "meshvolume: gpu" "$R/mongod.log" | cut -c1-400 | head -3
  [ "$up" = no ] && { tail -3 "$R/start.out" | cut -c1-500; grep -i "meshvolume\|\"s\":\"F\"" "$R/mongod.log" 2>/dev/null | cut -c1-500 | head -4; }
  if [ "$expect" = start ]; then chk "$name: server starts" "$([ "$up" = yes ] && echo ok || echo no)"; else chk "$name: server refuses to start" "$([ "$up" = no ] && echo ok || echo no)"; fi
  if [ "$up" = yes ] && [ -n "${PY:-}" ] && [ "$name" = default_absent ]; then "$PY" "$H/test_g5.py" 27555 2>&1 | tail -4 | tee "$R/test.out"; chk "$name: g5 test suite FAILS 0" "$(yes_if "$R/test.out" 'FAILS 0')"; fi
  kill $MP 2>/dev/null; wait $MP 2>/dev/null; echo "$?" > "$R/exit.rc"; sleep 1
  CASE_UP=$up; CASE_DIR=$R
}
PY="${PY:-}"
want default_absent && { one default_absent "" start;           chk "default_absent: requested=false logged" "$(yes_if $CASE_DIR/mongod.log 'requested=false')"; chk "default_absent: no Vulkan library mapped" "$(yes_if $CASE_DIR/maps.count 'mappings: 0')"; }
want empty_map && { one empty_map "extensionOptions: {}" start;  chk "empty_map: requested=false logged" "$(yes_if $CASE_DIR/mongod.log 'requested=false')"; chk "empty_map: no Vulkan library mapped" "$(yes_if $CASE_DIR/maps.count 'mappings: 0')"; }
want gpu_false && { one gpu_false $'extensionOptions:\n  gpu: false' start; chk "gpu_false: requested=false logged" "$(yes_if $CASE_DIR/mongod.log 'requested=false')"; chk "gpu_false: no Vulkan library mapped" "$(yes_if $CASE_DIR/maps.count 'mappings: 0')"; }
want gpu_true && { one gpu_true $'extensionOptions:\n  gpu: true' start; chk "gpu_true: logged requested=true" "$(yes_if $CASE_DIR/mongod.log 'requested=true')"; echo "   (on lavapipe the expected reason is: software device only)"; grep -o 'ready=[a-z]* engine=cpu reason=[^"]*' $CASE_DIR/mongod.log | head -2; }
want gpu_required && { one gpu_required $'extensionOptions:\n  gpu: true\n  gpuRequired: true' refuse; chk "gpu_required: reason in output or log" "$([ "$(yes_if $CASE_DIR/start.out 'gpuRequired')" = ok ] || [ "$(yes_if $CASE_DIR/mongod.log 'gpuRequired')" = ok ] && echo ok || echo no)"; }
want unknown_opt && { one unknown_opt $'extensionOptions:\n  frobnicate: true' refuse; chk "unknown_opt: reason in output or log" "$([ "$(yes_if $CASE_DIR/start.out 'frobnicate')" = ok ] || [ "$(yes_if $CASE_DIR/mongod.log 'frobnicate')" = ok ] && echo ok || echo no)"; }
want gpu_true_noloader && { one gpu_true_noloader $'extensionOptions:\n  gpu: true' start; chk "gpu_true_noloader: reason loader not found logged" "$(yes_if $CASE_DIR/mongod.log 'reason=vulkan loader not found')"; chk "gpu_true_noloader: clean exit on SIGTERM (rc 0)" "$([ "$(cat $CASE_DIR/exit.rc)" = 0 ] && echo ok || echo no)"; chk "gpu_true_noloader: no abruptQuit/backtrace in log" "$([ "$(grep -c 'abruptQuit\|BACKTRACE' $CASE_DIR/mongod.log)" = 0 ] && echo ok || echo no)"; }
echo "FAILS $FAILS"
