$repoRoot = Split-Path -Parent $PSScriptRoot
$runtimeLog = Join-Path $repoRoot "archi_runtime.log"
$traceLog = Join-Path $repoRoot "archi_trace.log"

if (-not (Test-Path $runtimeLog)) {
    New-Item -ItemType File -Path $runtimeLog -Force | Out-Null
}

if (-not (Test-Path $traceLog)) {
    New-Item -ItemType File -Path $traceLog -Force | Out-Null
}

$runtimeCommand = "Get-Content -Path '$runtimeLog' -Wait"
$traceCommand = "Get-Content -Path '$traceLog' -Wait"

Start-Process powershell -ArgumentList "-NoExit", "-Command", "`$Host.UI.RawUI.WindowTitle = 'Archi Runtime Log'; $runtimeCommand"
Start-Process powershell -ArgumentList "-NoExit", "-Command", "`$Host.UI.RawUI.WindowTitle = 'Archi Trace Log'; $traceCommand"
