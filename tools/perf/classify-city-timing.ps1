# Classify city frames (draws >= 800) whose draw_ms exceeds the 60fps budget.
# Dominant spike is argmax of the consumable CPU/wait timers already in the log.
param(
    [Parameter(Mandatory = $true)][string]$LogPath,
    [double]$BudgetMs = 16.67,
    [int]$CityDraws = 800
)

if (-not (Test-Path -LiteralPath $LogPath)) {
    throw "log not found: $LogPath"
}

$spikeKeys = @(
    'fence_wait_ms',
    'nested_flush_ms',
    'vertex_ms',
    'bind_ms',
    'record_ms',
    'rt_acquire_ms',
    'taa_ms'
)

$city = @()
$pattern = 'render timing frame=(\d+) draws=(\d+).*draw_ms=([0-9.]+).*vertex_ms=([0-9.]+).*bind_ms=([0-9.]+).*record_ms=([0-9.]+).*fence_wait_ms=([0-9.]+).*rt_acquire_ms=([0-9.]+).*taa_ms=([0-9.]+).*nested_flush_ms=([0-9.]+)(?:.*shader_lookup_ms=([0-9.]+).*pipeline_lookup_ms=([0-9.]+).*scene_copy_ms=([0-9.]+))?'
Select-String -LiteralPath $LogPath -Pattern $pattern | ForEach-Object {
    $g = $_.Matches[0].Groups
    $draws = [int]$g[2].Value
    if ($draws -lt $CityDraws) { return }
    $city += [pscustomobject]@{
        frame = [int]$g[1].Value
        draws = $draws
        draw_ms = [double]$g[3].Value
        vertex_ms = [double]$g[4].Value
        bind_ms = [double]$g[5].Value
        record_ms = [double]$g[6].Value
        fence_wait_ms = [double]$g[7].Value
        rt_acquire_ms = [double]$g[8].Value
        taa_ms = [double]$g[9].Value
        nested_flush_ms = [double]$g[10].Value
        shader_lookup_ms = if ($g[11].Success) { [double]$g[11].Value } else { 0 }
        pipeline_lookup_ms = if ($g[12].Success) { [double]$g[12].Value } else { 0 }
        scene_copy_ms = if ($g[13].Success) { [double]$g[13].Value } else { 0 }
    }
}

function Percentile($arr, $prop, $p) {
    if (-not $arr -or $arr.Count -eq 0) { return $null }
    $sorted = @($arr | ForEach-Object { $_.$prop } | Sort-Object)
    $idx = [math]::Min($sorted.Count - 1, [math]::Max(0, [math]::Ceiling($p * $sorted.Count) - 1))
    [math]::Round($sorted[$idx], 3)
}

$over = @($city | Where-Object { $_.draw_ms -gt $BudgetMs })
$buckets = [ordered]@{}
foreach ($key in $spikeKeys) { $buckets[$key] = 0 }
foreach ($row in $over) {
    $best = $spikeKeys[0]
    $bestValue = $row.$best
    foreach ($key in $spikeKeys) {
        if ($row.$key -gt $bestValue) {
            $best = $key
            $bestValue = $row.$key
        }
    }
    $buckets[$best]++
}

$summary = [ordered]@{
    log = (Resolve-Path -LiteralPath $LogPath).Path
    city_frames = $city.Count
    budget_ms = $BudgetMs
    over_budget = $over.Count
    over_budget_pct = if ($city.Count) { [math]::Round(100.0 * $over.Count / $city.Count, 2) } else { 0 }
    draw_ms_mean = if ($city.Count) { [math]::Round((($city | Measure-Object draw_ms -Average).Average), 3) } else { $null }
    draw_ms_p95 = Percentile $city 'draw_ms' 0.95
    draw_ms_p99 = Percentile $city 'draw_ms' 0.99
    draw_ms_max = if ($city.Count) { [math]::Round((($city | Measure-Object draw_ms -Maximum).Maximum), 3) } else { $null }
    nested_flush_ms_max = if ($city.Count) { [math]::Round((($city | Measure-Object nested_flush_ms -Maximum).Maximum), 3) } else { $null }
    shader_lookup_ms_mean = if ($city.Count) { [math]::Round((($city | Measure-Object shader_lookup_ms -Average).Average), 3) } else { $null }
    pipeline_lookup_ms_mean = if ($city.Count) { [math]::Round((($city | Measure-Object pipeline_lookup_ms -Average).Average), 3) } else { $null }
    scene_copy_ms_mean = if ($city.Count) { [math]::Round((($city | Measure-Object scene_copy_ms -Average).Average), 3) } else { $null }
    dominant_spike = $buckets
}

$summary | ConvertTo-Json -Depth 5
Write-Host ("city={0} over={1} ({2}%) draw_ms mean={3} p95={4} p99={5} max={6} nested_flush_max={7}" -f `
    $summary.city_frames, $summary.over_budget, $summary.over_budget_pct, `
    $summary.draw_ms_mean, $summary.draw_ms_p95, $summary.draw_ms_p99, $summary.draw_ms_max, `
    $summary.nested_flush_ms_max)
foreach ($key in $spikeKeys) {
    Write-Host ("  {0}={1}" -f $key, $buckets[$key])
}
