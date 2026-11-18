# MP16 step 4: the real Windows mongod with the extension DLL. Usage (non-elevated):
#   powershell -File mp16d_run.ps1 -Mongod B:\mongo\bazel-bin\install\bin\mongod.exe -Dll <meshvol_ext.dll> -Models <real60 dir> -Py <python.exe with pymongo> -Out B:\mp16d_out
# -Cases cpu_default,gpu,gpurequired selects which run (default all; GPU cases only under a held GPU turn). Cases: cpu_default (no options), gpu (gpu: true, native NVIDIA driver), gpurequired (gpu+gpuRequired).
# Each case: start mongod, wait for "Waiting for connections", run test_mp14d.py, stop, grep the log for fatal errors.
param([string]$Mongod,[string]$Dll,[string]$Models,[string]$Py,[string]$Out,[int]$Port=27555,[string[]]$Cases=@('cpu_default','gpu','gpurequired'))
$ErrorActionPreference='Continue'; $fails=0; $h=$PSScriptRoot
New-Item -ItemType Directory -Force $Out | Out-Null
function Run-Case($name,$opts,$envs){
  $r=Join-Path $Out "run_$name"; New-Item -ItemType Directory -Force "$r\db","$r\conf" | Out-Null
  $dll=$Dll -replace '\\','/'
  $conf="sharedLibraryPath: $dll`n$opts`n"; Set-Content "$r\conf\meshvol_ext.conf" $conf -Encoding ascii
  $rr=$r -replace '\\','/'
  Set-Content "$r\mongod.conf" @"
processManagement: {loadExtensions: [meshvol_ext], extensionsConfigPath: $rr/conf}
setParameter: {featureFlagExtensionsAPI: true}
net: {port: $Port, bindIp: 127.0.0.1}
storage: {dbPath: $rr/db}
systemLog: {destination: file, path: $rr/mongod.log}
"@ -Encoding ascii
  "== case $name; options: [$($opts -replace "`n",'|')]"
  $p=Start-Process $Mongod -ArgumentList '-f',"$r\mongod.conf" -PassThru -RedirectStandardOutput "$r\start.out" -RedirectStandardError "$r\start.err" -WindowStyle Hidden
  $up=$false; for($i=0;$i -lt 90;$i++){ if((Test-Path "$r\mongod.log") -and (Select-String -Path "$r\mongod.log" -Pattern 'Waiting for connections' -Quiet)){$up=$true;break}; if($p.HasExited){break}; Start-Sleep 1 }
  if($up){
    foreach($kv in $envs.GetEnumerator()){ Set-Item "env:$($kv.Key)" $kv.Value }
    & $Py "$h\test_mp14d.py" $Port $Models 2>&1 | Tee-Object "$r\test.out"
    foreach($kv in $envs.GetEnumerator()){ Remove-Item "env:$($kv.Key)" }
    if(Select-String -Path "$r\test.out" -Pattern '^FAILS 0' -Quiet){"PASS ${name}: test_mp14d FAILS 0"}else{"FAIL ${name}: test_mp14d";$script:fails++}
  } else { "FAIL ${name}: server did not start"; Get-Content "$r\start.err","$r\start.out" -ErrorAction SilentlyContinue | Select-Object -Last 5; $script:fails++ }
  if(-not $p.HasExited){ Stop-Process $p -Force; $p.WaitForExit(20000) | Out-Null }
  $sig=(Select-String -Path "$r\mongod.log" -Pattern 'Got signal|Invalid access|abruptQuit|Fatal assertion' -ErrorAction SilentlyContinue | Measure-Object).Count
  if($sig -eq 0){"PASS ${name}: no fatal lines in the server log"}else{"FAIL ${name}: $sig fatal lines in the server log";$script:fails++}
  Select-String -Path "$r\mongod.log" -Pattern 'meshvolume: gpu' | Select-Object -First 2 | ForEach-Object { $_.Line.Substring(0,[Math]::Min(300,$_.Line.Length)) }
}
if($Cases -contains 'cpu_default'){ Run-Case 'cpu_default' '' @{} }
if($Cases -contains 'gpu'){ Run-Case 'gpu' "extensionOptions:`n  gpu: true" @{EXPECT_GPU='1'; MP14D_GPU_MIN='1'} }
if($Cases -contains 'gpurequired'){ Run-Case 'gpurequired' "extensionOptions:`n  gpu: true`n  gpuRequired: true" @{EXPECT_GPU='1'; MP14D_GPU_MIN='1'} }
"FAILS $fails"
