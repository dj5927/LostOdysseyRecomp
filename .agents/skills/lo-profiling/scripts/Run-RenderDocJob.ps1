param(
    [Parameter(Mandatory = $true)][string]$Job,
    [string]$RenderDocDirectory = $env:RENDERDOC_HOME,
    [string]$ExpectedSha256,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$jobFile = (Resolve-Path -LiteralPath $Job).Path
$config = Get-Content -Raw -LiteralPath $jobFile | ConvertFrom-Json
$requirements = @{
    probe = @('output')
    launch = @('output', 'executable', 'workingDirectory')
    status = @('output', 'ident')
    capture = @('output', 'ident')
    analyse = @('output', 'capture')
    ab = @('output', 'capture', 'replacement', 'originalSha256', 'shaderEventId', 'hotspotEventIds', 'imageEventId')
    structure = @('output', 'capture', 'event_window')
}
if (!$requirements.ContainsKey([string]$config.mode)) { throw 'Unknown or missing job mode' }
foreach ($field in $requirements[$config.mode]) {
    if ($null -eq $config.$field -or [string]$config.$field -eq '') { throw "Missing job field: $field" }
}
foreach ($field in @('output', 'capture', 'replacement', 'executable', 'workingDirectory', 'cacheDirectory')) {
    if ($config.$field -and ![IO.Path]::IsPathRooted([string]$config.$field)) { throw "$field must be an absolute path" }
}
foreach ($field in @('capture', 'replacement', 'executable')) {
    if ($config.$field -and !(Test-Path -LiteralPath $config.$field -PathType Leaf)) { throw "Missing file: $field" }
}
if ($config.mode -in @('analyse', 'ab', 'structure')) {
    if ((Test-Path -LiteralPath $config.output) -and (Get-ChildItem -LiteralPath $config.output -Force | Select-Object -First 1)) {
        throw 'Replay output must be a new or empty directory; preserve previous evidence'
    }
}
if ($config.mode -in @('capture', 'status') -and [int]$config.ident -le 0) { throw 'ident must be positive' }
if ($config.mode -eq 'ab') {
    if ($config.originalSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'originalSha256 must be a SHA256 digest' }
    if ($config.shaderStage -and $config.shaderStage -notin @('Pixel', 'Vertex')) { throw 'AB supports Pixel or Vertex shaders' }
    if (@($config.hotspotEventIds).Count -eq 0) { throw 'hotspotEventIds must not be empty' }
    foreach ($eventId in @($config.shaderEventId, $config.imageEventId) + @($config.hotspotEventIds)) {
        if ([int]$eventId -le 0) { throw 'Event IDs must be positive' }
    }
    if ($null -ne $config.outputTargetIndex -and [int]$config.outputTargetIndex -lt 0) { throw 'outputTargetIndex must be nonnegative' }
}
if ($config.mode -eq 'structure' -and (@($config.event_window).Count -ne 2 -or
    [int]$config.event_window[0] -lt 1 -or [int]$config.event_window[1] -lt [int]$config.event_window[0])) {
    throw 'event_window must be [first positive event, last event]'
}
if ($config.mode -eq 'launch') {
    if (!(Test-Path -LiteralPath $config.workingDirectory -PathType Container)) { throw 'Working directory missing' }
    if ($ExpectedSha256 -and (Get-FileHash -LiteralPath $config.executable -Algorithm SHA256).Hash -ne $ExpectedSha256) {
        throw 'Target EXE SHA256 differs from ExpectedSha256'
    }
    # Enumerate first; a provider-filtered query previously missed a live process.
    $running = Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $config.executable }
    if ($running) { throw 'Target executable is already running' }
}
$workerName = switch ($config.mode) {
    ab { 'renderdoc_ab_worker.py' }
    structure { 'renderdoc_structure_worker.py' }
    default { 'renderdoc_worker.py' }
}
$worker = Join-Path $PSScriptRoot $workerName
if ($RenderDocDirectory) { $runner = Join-Path $RenderDocDirectory 'qrenderdoc.exe' }
else {
    $command = Get-Command qrenderdoc.exe -ErrorAction SilentlyContinue
    if (!$command) { throw 'Pass -RenderDocDirectory or set RENDERDOC_HOME to the RenderDoc directory' }
    $runner = $command.Source
}
$runner = (Resolve-Path -LiteralPath $runner).Path
if ($ValidateOnly) {
    [pscustomobject]@{ Valid = $true; Mode = $config.mode; Worker = $worker; Runner = $runner; Output = $config.output }
    return
}
$savedJob = $env:LO_RENDERDOC_JOB
$savedQt = $env:QT_QPA_PLATFORM
try {
    $env:LO_RENDERDOC_JOB = $jobFile
    # The Windows portable build includes qwindows.dll, not an offscreen plugin.
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $runner -ArgumentList @('--python', ('"' + $worker + '"')) -WindowStyle Hidden -PassThru
    [pscustomobject]@{ HelperPid = $process.Id; Job = $jobFile; Output = $config.output; Mode = $config.mode }
} finally {
    $env:LO_RENDERDOC_JOB = $savedJob
    $env:QT_QPA_PLATFORM = $savedQt
}
