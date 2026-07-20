# Multi-worker 48h JIT-vs-interpreter differential soak for the Windows x64 native build (--gen driver).
# Each worker walks a disjoint seed band (bands partition the uint32 seed space so workers never re-run the same
# program), loops --gen chunks until the deadline, and STOPS its band on the first non-zero exit (a divergence:
# GAZLFuzz.exe abort()s and dumps the offending program to the log). Logs: <repo>\fuzzlogs\fz<N>.log.
#   powershell -ExecutionPolicy Bypass -File tools\fuzzSoak.ps1 -Workers 6 -Hours 48
param([int]$Workers = 6, [double]$Hours = 48, [int]$Chunk = 1000000)
$root = "C:\Users\ClaudeRunner\git\GAZL"
$exe  = Join-Path $root "output\GAZLFuzz.exe"
if (-not (Test-Path $exe)) { throw "build first: tools\buildGazlFuzz.cmd" }
$logs = Join-Path $root "fuzzlogs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$deadline = (Get-Date).AddHours($Hours)
$span = [int64](4200000000 / $Workers)   # disjoint bands, all within uint32 (strtoul limit)
$jobs = @()
for ($w = 0; $w -lt $Workers; $w++) {
  $base = [int64]$w * $span
  $log  = Join-Path $logs ("fz{0}.log" -f $w)
  "worker $w : seeds [$base .. $($base + $span)) -> $log"
  $jobs += Start-Job -ArgumentList $exe,$base,$span,$Chunk,$deadline,$log -ScriptBlock {
    param($exe,$base,$span,$chunk,$deadline,$log)
    $seed = $base; $end = $base + $span
    while ((Get-Date) -lt $deadline -and $seed -lt $end) {
      & $exe --gen $chunk $seed deep *>> $log
      if ($LASTEXITCODE -ne 0) { "STOP DIVERGENCE seed=$seed rc=$LASTEXITCODE" | Out-File -Append $log; break }
      $seed += $chunk
    }
  }
}
"Started $Workers workers; deadline $deadline. Watch: type fuzzlogs\fz*.log | findstr /I divergence STOP"
$jobs | Wait-Job | Out-Null
$hits = Select-String -Path (Join-Path $logs "fz*.log") -Pattern "divergence","STOP DIVERGENCE" -SimpleMatch
if ($hits) { "!!! DIVERGENCE FOUND:"; $hits } else { "48h soak complete - no divergences." }
