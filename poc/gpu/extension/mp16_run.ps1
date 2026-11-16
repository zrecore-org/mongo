# MP16 step 1: the extension and its test host natively on Windows (cross-built with mingw-w64 on the M1; nothing is built here). Windows PowerShell 5.1 or later.
# Usage: powershell -ExecutionPolicy Bypass -File mp16_run.ps1 -Item core|host|real|gpu -Pkg <this package dir> -Out <empty output dir> [-Models <dir with the 60 .stl files>] [-GpuTurn]
#   core: CPU only (exactness suites and their mutants).  host: CPU only (stage plumbing through the DLL).  real: CPU only (60 real models, needs -Models).
#   gpu:  uses the GPU, ONLY inside a GPU turn held by the mongo agent (one program at a time), refuses without -GpuTurn.
# Nothing is installed and no setting is changed; files are written only under -Out. Exit code 0 only if every check passes.
param([Parameter(Mandatory)][ValidateSet('core','host','real','gpu','bench')][string]$Item, [Parameter(Mandatory)][string]$Pkg, [Parameter(Mandatory)][string]$Out,
      [string]$Models = '', [switch]$GpuTurn)
$ErrorActionPreference = 'Continue'
[Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
if (($Item -eq 'gpu' -or $Item -eq 'bench') -and -not $GpuTurn) { throw "$Item uses the GPU: run it only inside a GPU turn and pass -GpuTurn" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Exe = Join-Path $Pkg 'exe'; $Dll = Join-Path $Exe 'libmeshvol_ext.dll'
$script:Fails = 0
function Run-Prog($name, $exe, $argList, $limitSec, $envs) {
    $log = Join-Path $Out "$name.log"; $err = Join-Path $Out "$name.err"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    if ($envs) { foreach ($k in $envs.Keys) { Set-Item -Path "Env:$k" -Value $envs[$k] } }
    $sp = @{ FilePath = $exe; WorkingDirectory = $Out; NoNewWindow = $true; PassThru = $true; RedirectStandardOutput = $log; RedirectStandardError = $err }
    if ($argList -and $argList.Count -gt 0) { $sp.ArgumentList = $argList }
    $p = Start-Process @sp
    $null = $p.Handle
    if (-not $p.WaitForExit($limitSec * 1000)) { $p.Kill(); $rc = 124 } else { $rc = $p.ExitCode }
    if ($null -eq $rc) { $rc = -999 }
    if ($envs) { foreach ($k in $envs.Keys) { Remove-Item -Path "Env:$k" -ErrorAction SilentlyContinue } }
    $sw.Stop()
    $text = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
    if ($null -eq $text) { $text = '' }
    Add-Content -Path $log -Value ("rc=$rc seconds=" + [math]::Round($sw.Elapsed.TotalSeconds,1))
    return [pscustomobject]@{ Name = $name; Rc = $rc; Sec = [math]::Round($sw.Elapsed.TotalSeconds,1); Text = $text }
}
function Chk($name, $ok, $extra) { if ($ok) { "PASS $name $extra" } else { "FAIL $name $extra"; $script:Fails++ } }
function Cnt($text, $field, $nth) { $m = [regex]::Matches($text, "(?m)^COUNTERS.*?$field=(\d+)"); if ($m.Count -ge $nth) { return [int]$m[$nth-1].Groups[1].Value } else { return -1 } }
function Has($text, $pat) { return [regex]::IsMatch($text, $pat) }

if ($Item -eq 'core') {
    $r = Run-Prog 'test_contains_core' (Join-Path $Exe 'test_contains_core.exe') @() 900 $null
    Chk 'core suite vs the exactmesh oracle: 0 disagreements' ($r.Rc -eq 0 -and (Has $r.Text 'TOTAL queries=\d+ .* disagreements=0')) ((($r.Text -split "`n") | Select-String 'TOTAL queries' | Select-Object -First 1).ToString().Trim())
    $r = Run-Prog 'test_contains_ind' (Join-Path $Exe 'test_contains_ind.exe') @() 900 $null
    Chk 'independent suite (analytic, metamorphic, predicates, coverage): FAILS 0' ($r.Rc -eq 0 -and (Has $r.Text 'FAILS 0')) ''
}
if ($Item -eq 'host') {
    $r = Run-Prog 'fakehost_contains' (Join-Path $Exe 'fakehost.exe') @('contains', ($Dll -replace '\\','/')) 900 $null
    Chk 'stage plumbing through the DLL (fake host contains): FAILS 0, no FAIL line' ($r.Rc -eq 0 -and (Has $r.Text 'FAILS 0') -and -not (Has $r.Text '(?m)^FAIL ')) ''
    $r = Run-Prog 'fakehost_options_default' (Join-Path $Exe 'fakehost.exe') @('options', ($Dll -replace '\\','/'), '{}') 900 $null
    Chk 'default off: loads, gpuRequested=false, vulkanProbes=0, no Vulkan module loaded, corpus exact' ((Has $r.Text 'OPTIONS-LOAD ok=1') -and (Has $r.Text 'gpuRequested=false') -and (Has $r.Text 'vulkanProbes=0') -and (Has $r.Text 'VULKAN-IMAGES 0') -and (Has $r.Text 'wrong=0 exactMismatch=0 roundingWrong=0')) ''
    foreach ($bad in 'frobnicate: true','gpu: maybe') {
        $r = Run-Prog 'fakehost_options_bad' (Join-Path $Exe 'fakehost.exe') @('options', ($Dll -replace '\\','/'), "`"$bad`"") 300 $null
        Chk "strict options: '$bad' refuses to load" (Has $r.Text 'OPTIONS-LOAD ok=0') ''
    }
}
if ($Item -eq 'real' -or $Item -eq 'gpu') {
    if (-not $Models) { throw "$Item needs -Models <dir with the 60 .stl files>" }
    $list = Join-Path $Out 'list_win.txt'
    Get-Content (Join-Path $Pkg 'mp12e_model_ids.txt') | Where-Object { $_.Trim() } | ForEach-Object { (Join-Path $Models ($_.Trim() + '.stl')) -replace '\\','/' } | Set-Content -Path $list
    $missing = Get-Content $list | Where-Object { -not (Test-Path $_) }
    if ($missing) { throw "missing model files: $($missing.Count), first: $($missing | Select-Object -First 1)" }
}
if ($Item -eq 'real') {
    $r = Run-Prog 'real_contains' (Join-Path $Exe 'real_contains.exe') @(($list -replace '\\','/'), '3') 1800 $null
    Chk 'CPU path on the 60 real models (540,000 points): 0 disagreements, hash ae59d9976297a67b' ($r.Rc -eq 0 -and (Has $r.Text 'ON 68436\)') -and (Has $r.Text 'disagreements=0 resolved-verdict hash=ae59d9976297a67b')) ''
}
if ($Item -eq 'gpu') {
    & nvidia-smi --query-gpu=name,driver_version,memory.used --format=csv | Tee-Object -FilePath (Join-Path $Out 'nvidia-smi_before.txt')
    $env:VK_LOADER_DEBUG = ''
    $vi = Get-Command vulkaninfo -ErrorAction SilentlyContinue
    if ($vi) { & $vi.Source --summary 2>&1 | Select-String 'deviceName|deviceType|driverName|driverInfo|apiVersion' | Select-Object -First 12 | Tee-Object -FilePath (Join-Path $Out 'vulkaninfo.txt') }
    $fh = Join-Path $Exe 'fakehost.exe'; $d = ($Dll -replace '\\','/')
    function Case($label, $dll, $case, $n, $envs) { $a = @('containsgpu', $dll, $case); if ($n) { $a += "$n" }; return (Run-Prog "gpu_$label" $fh $a 900 $envs) }
    $r = Case 'ok' $d 'ok' 100000 $null
    Chk 'device path used (NVIDIA driver): 1 then 2 batches, verdicts equal the CPU core on both runs, UNCERTAIN resolved exactly, engine gpu' ((Has $r.Text 'CASE ok.*equal CPU core: YES') -and (Has $r.Text 'SECOND RUN verdicts equal CPU core: YES') -and ((Cnt $r.Text 'gpuBatches' 1) -eq 1) -and ((Cnt $r.Text 'gpuBatches' 2) -eq 2) -and ((Cnt $r.Text 'gpuUncertain' 1) -gt 0) -and (Has $r.Text 'engine="gpu"') -and (Has $r.Text 'hardware device present')) ((($r.Text -split "`n") | Select-String '^COUNTERS' | Select-Object -First 1).ToString().Trim())
    $r = Case 'small' $d 'small' 1000 $null;  Chk 'below the threshold: CPU, 0 device batches' ((Has $r.Text 'equal CPU core: YES') -and ((Cnt $r.Text 'gpuBatches' 1) -eq 0)) ''
    $r = Case 'cpu' $d 'cpu' 100000 $null;    Chk 'engine knob cpu: 0 device batches' ((Has $r.Text 'equal CPU core: YES') -and ((Cnt $r.Text 'gpuBatches' 1) -eq 0)) ''
    $r = Case 'fail' $d 'fail' 100000 $null;  Chk 'planted device failure: fall back, verdicts equal, breaker open' ((Has $r.Text 'CASE fail.*equal CPU core: YES') -and (Has $r.Text 'SECOND RUN verdicts equal CPU core: YES') -and ((Cnt $r.Text 'gpuFallbacks' 2) -eq 1) -and ((Cnt $r.Text 'gpuBatches' 2) -eq 0)) ''
    $r = Case 'wrong' $d 'wrong' 100000 $null; Chk 'planted wrong verdicts: shadow check catches, verdicts equal, device disabled' ((Has $r.Text 'CASE wrong.*equal CPU core: YES') -and (Has $r.Text 'SECOND RUN verdicts equal CPU core: YES') -and ((Cnt $r.Text 'gpuShadowMismatch' 2) -eq 1) -and ((Cnt $r.Text 'gpuBatches' 2) -eq 0)) ''
    foreach ($pc in @(@('createfail','planted failure of the lazy device creation'), @('oom','vkCreateBuffer|vkAllocateMemory'), @('timeout','vkWaitForFences'))) {
        $r = Case $pc[0] $d $pc[0] 20000 $null
        Chk "planted $($pc[0]): the intended failure fired, fell back, verdicts equal, device disabled" ((Has $r.Text 'CASE .*equal CPU core: YES') -and (Has $r.Text 'SECOND RUN verdicts equal CPU core: YES') -and (Has $r.Text ('gpuLastFailure="[^"]*(' + $(if ($pc[0] -eq 'oom') { $pc[1] } else { [regex]::Escape($pc[1]) }) + ')')) -and ((Cnt $r.Text 'gpuFallbacks' 2) -eq 1)) ((($r.Text -split "`n") | Select-String 'gpuLastFailure' | Select-Object -First 1).ToString().Trim().Substring(0,[math]::Min(300,(($r.Text -split "`n") | Select-String 'gpuLastFailure' | Select-Object -First 1).ToString().Trim().Length)))
    }
    $r = Case 'concurrent' $d 'concurrent' 20000 $null; Chk 'concurrency: 6 threads x 6 aggregations, 0 mismatching runs, 36 device batches' ((Has $r.Text 'verdict-mismatching runs=0') -and ((Cnt $r.Text 'gpuBatches' 1) -eq 36)) ''
    $r50 = Case 'repeat450' $d 'repeat' 20000 @{ MP14D_REPEAT = '450' }; $r450 = Case 'repeat1800' $d 'repeat' 20000 @{ MP14D_REPEAT = '1800' }
    $g50 = [regex]::Match($r50.Text, 'rssGrowth=(\d+)').Groups[1].Value; $g450 = [regex]::Match($r450.Text, 'rssGrowth=(\d+)').Groups[1].Value
    Chk 'no leak: CURRENT private bytes growth between 450 and 1800 device runs under 16 MB (after warm-up)' (($g50 -ne '') -and ($g450 -ne '') -and (([int64]$g450 - [int64]$g50) -lt 16000000)) "450 runs: $g50, 1800 runs: $g450"
    $mut = @(@(1,'wrong','CASE wrong.*equal CPU core: NO'), @(2,'fail',''), @(3,'small',''), @(4,'ok','CASE ok.*equal CPU core: NO'))
    foreach ($m in $mut) {
        $md = (Join-Path $Exe "libmeshvol_gpumut$($m[0]).dll") -replace '\\','/'
        $r = Case "mut$($m[0])" $md $m[1] $(if ($m[1] -eq 'small') { 1000 } else { 100000 }) $null
        $caught = switch ($m[0]) { 1 { Has $r.Text $m[2] } 2 { (Cnt $r.Text 'gpuFallbacks' 2) -eq 2 } 3 { (Cnt $r.Text 'gpuBatches' 1) -gt 0 } 4 { Has $r.Text $m[2] } }
        Chk "device-path mutant $($m[0]) is caught" $caught ''
    }
    $r = Run-Prog 'real_gpu' (Join-Path $Exe 'real_gpu.exe') @(($list -replace '\\','/')) 1800 $null
    Chk 'device path on the 60 real models through the NVIDIA driver: 0 false definite, 0 disagreements, hash ae59d9976297a67b, UNCERTAIN 134979' ($r.Rc -eq 0 -and (Has $r.Text 'uncertain=134979 falseDefinite=0 ON=68436 disagreements=0 resolved-verdict hash=ae59d9976297a67b')) ((($r.Text -split "`n") | Select-String '^TOTAL' | Select-Object -First 1).ToString().Trim())
    & nvidia-smi --query-gpu=name,driver_version,memory.used --format=csv | Tee-Object -FilePath (Join-Path $Out 'nvidia-smi_after.txt')
}
if ($Item -eq 'bench') {
    # LABELLED speed table: CPU path with 1, 2, 4, 6, 12 threads versus the device path, warm second run of one document (9,600-triangle sphere), N random points. Not a G4 claim by itself.
    & nvidia-smi --query-gpu=name,driver_version,memory.used --format=csv | Tee-Object -FilePath (Join-Path $Out 'nvidia-smi_before.txt')
    $fh = Join-Path $Exe 'fakehost.exe'; $d = ($Dll -replace '\\','/')
    $rows = @("points | cpu 1 | cpu 2 | cpu 4 | cpu 6 | cpu 12 | device   (ms, warm second run)")
    foreach ($n in 50000, 200000, 600000) {
        $row = "$n"
        foreach ($t in 1, 2, 4, 6, 12) {
            $r = Run-Prog "bench_cpu_${n}_t$t" $fh @('containsgpu', $d, 'cpu', "$n") 900 @{ MP14D_CPU_THREADS = "$t" }
            $row += ' | ' + [regex]::Match($r.Text, 'SECOND RUN elapsedMs=([0-9.]+)').Groups[1].Value
            Chk "cpu path ($t threads, $n points): verdicts equal the single-thread core" (Has $r.Text 'equal CPU core: YES') ''
        }
        $r = Run-Prog "bench_dev_$n" $fh @('containsgpu', $d, 'ok', "$n") 900 $null
        $row += ' | ' + [regex]::Match($r.Text, 'SECOND RUN elapsedMs=([0-9.]+)').Groups[1].Value
        $rows += $row
    }
    $rows | Tee-Object -FilePath (Join-Path $Out 'bench_table.txt')
    & nvidia-smi --query-gpu=name,driver_version,memory.used --format=csv | Tee-Object -FilePath (Join-Path $Out 'nvidia-smi_after.txt')
}
"FAILS $($script:Fails)"
$sums = Get-ChildItem $Out -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash.ToLower())  $($_.Path | Split-Path -Leaf)" }
$sums | Set-Content -Path (Join-Path $Out 'SHA256SUMS.txt')
exit $(if ($script:Fails) { 1 } else { 0 })
