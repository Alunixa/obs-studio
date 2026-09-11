param(
    [Parameter(Mandatory)][string]$Reference,
    [Parameter(Mandatory)][string]$Recording,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [double]$SampleTime = 1,
    [double]$MinimumSSIM = 0.995,
    [double]$MinimumPSNR = 45
)
$ErrorActionPreference = 'Stop'
$Reference = (Resolve-Path -LiteralPath $Reference).Path
$Recording = (Resolve-Path -LiteralPath $Recording).Path
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new verification output directory' }
if ($SampleTime -lt 0 -or $MinimumSSIM -le 0 -or $MinimumSSIM -gt 1 -or $MinimumPSNR -le 0) {
    throw 'Invalid quality comparison parameters'
}
$null = Get-Command ffmpeg -ErrorAction Stop
$null = Get-Command ffprobe -ErrorAction Stop
$null = New-Item -ItemType Directory -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$Culture = [Globalization.CultureInfo]::InvariantCulture
$ProbeText = & ffprobe -v error -select_streams v:0 `
    -show_entries 'stream=codec_name,width,height,pix_fmt,r_frame_rate' -of json $Recording
if ($LASTEXITCODE) { throw 'Could not read recording metadata' }
$Stream = @(($ProbeText | ConvertFrom-Json).streams)[0]
if ($Stream.width -ne 2560 -or $Stream.height -ne 1440 -or $Stream.r_frame_rate -ne '60/1') {
    throw 'The native-detail fixture requires unscaled 2560x1440 at 60 fps'
}
$Frame = Join-Path $OutputDirectory 'decoded.png'
& ffmpeg -hide_banner -loglevel error -xerror -ss $SampleTime.ToString($Culture) `
    -i $Recording -map 0:v:0 -frames:v 1 -update 1 $Frame
if ($LASTEXITCODE -or -not (Test-Path -LiteralPath $Frame)) { throw 'Could not decode the sample frame' }

$Metrics = @{}
foreach ($Metric in @('ssim', 'psnr')) {
    $Log = @(& ffmpeg -hide_banner -i $Frame -i $Reference `
        -lavfi "[0:v]format=gbrp[a];[1:v]format=gbrp[b];[a][b]$Metric" `
        -frames:v 1 -f null - 2>&1)
    $ExitCode = $LASTEXITCODE
    $Log | Set-Content -LiteralPath (Join-Path $OutputDirectory "$Metric.log") -Encoding utf8
    if ($ExitCode) { throw "$Metric comparison failed" }
    $Pattern = if ($Metric -eq 'ssim') { 'All:([0-9.]+)' } else { 'average:([0-9.]+|inf)' }
    $Match = [regex]::Match(($Log -join "`n"), $Pattern)
    if (-not $Match.Success) { throw "Missing $Metric result" }
    $Value = $Match.Groups[1].Value
    $Metrics[$Metric] = if ($Value -eq 'inf') { [double]::PositiveInfinity } else {
        [double]::Parse($Value, $Culture)
    }
}
$Passed = $Metrics.ssim -ge $MinimumSSIM -and $Metrics.psnr -ge $MinimumPSNR
$Result = [ordered]@{
    Recording = [IO.Path]::GetFileName($Recording)
    RecordingSHA256 = (Get-FileHash -LiteralPath $Recording -Algorithm SHA256).Hash.ToLowerInvariant()
    ReferenceSHA256 = (Get-FileHash -LiteralPath $Reference -Algorithm SHA256).Hash.ToLowerInvariant()
    DecodedFrameSHA256 = (Get-FileHash -LiteralPath $Frame -Algorithm SHA256).Hash.ToLowerInvariant()
    Codec = $Stream.codec_name
    Width = $Stream.width
    Height = $Stream.height
    PixelFormat = $Stream.pix_fmt
    SampleTime = $SampleTime
    SSIM = $Metrics.ssim
    PSNR = if ([double]::IsPositiveInfinity($Metrics.psnr)) { 'inf' } else { $Metrics.psnr }
    MinimumSSIM = $MinimumSSIM
    MinimumPSNR = $MinimumPSNR
    Passed = $Passed
}
$Result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'quality.json') -Encoding utf8
if (-not $Passed) { throw "Detail fidelity failed: SSIM=$($Metrics.ssim), PSNR=$($Metrics.psnr)" }
Write-Output "PASS sampled native-detail fidelity: SSIM=$($Metrics.ssim), PSNR=$($Metrics.psnr) dB"
# This measures a static reference frame, not motion fidelity, dropped frames,
# capture-source quality, playback scaling, or performance under game load.
