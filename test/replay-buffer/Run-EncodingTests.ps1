param(
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string[]]$Cases = @('window-i444', 'window-nv12', 'window-p010', 'window-x264', 'window-ram',
        'quality-i444', 'quality-p010')
)
$ErrorActionPreference = 'Stop'
$Runtime = (Resolve-Path -LiteralPath $Runtime).Path
$Binary = Join-Path $Runtime 'bin/64bit/test-replay-encoding.exe'
if (-not (Test-Path -LiteralPath $Binary -PathType Leaf)) { throw 'Build the encoding test target first' }
$null = Get-Command ffmpeg -ErrorAction Stop
$null = Get-Command ffprobe -ErrorAction Stop
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$Matrix = @{
    'window-i444' = @('obs_nvenc_hevc_tex', 'I444', '1', 'window', 'yuv444p')
    'window-nv12' = @('obs_nvenc_h264_tex', 'NV12', '1', 'window', 'yuv420p')
    'window-p010' = @('obs_nvenc_hevc_tex', 'P010', '1', 'window', 'yuv420p10le')
    'window-x264' = @('obs_x264', 'NV12', '1', 'window', 'yuv420p')
    'window-ram' = @('obs_nvenc_hevc_tex', 'I444', '0', 'window', 'yuv444p')
    'quality-i444' = @('obs_nvenc_hevc_tex', 'I444', '1', '', 'yuv444p')
    'quality-p010' = @('obs_nvenc_hevc_tex', 'P010', '1', '', 'yuv420p10le')
}
$Manifest = Join-Path $OutputDirectory 'verification.json'
$Results = @()
if (Test-Path -LiteralPath $Manifest) {
    $Results = @(Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json)
}
foreach ($Name in $Cases) {
    if (-not $Matrix.ContainsKey($Name)) { throw "Unknown test case: $Name" }
    $Case = $Matrix[$Name]
    $Out = Join-Path $OutputDirectory "$Name-中文 (space)"
    if (Test-Path -LiteralPath $Out) { throw "Refusing to overwrite existing test output: $Out" }
    $null = New-Item -ItemType Directory -Path $Out
    $Arguments = @(('"' + $Runtime + '"'), ('"' + $Out + '"'), $Case[0], $Case[1], $Case[2])
    if ($Case[3]) { $Arguments += $Case[3] }
    $Process = Start-Process -FilePath $Binary -ArgumentList $Arguments `
        -WorkingDirectory (Join-Path $Runtime 'bin/64bit') -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $Out 'encode.log') `
        -RedirectStandardError (Join-Path $Out 'encode-error.log') -PassThru
    if (-not $Process.WaitForExit(120000)) {
        # Only the synthetic test process created above, never a user's OBS.
        $Process.Kill()
        throw "Synthetic encoding test timed out: $Name"
    }
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) { throw "Encoding failed: $Name; see logs in $Out" }
    $Files = @(Get-ChildItem -LiteralPath $Out -Filter '*.mkv' -File)
    $Expected = if ($Case[3]) { 4 } else { 3 }
    if ($Files.Count -ne $Expected) { throw "Wrong output count for $Name" }
    foreach ($File in $Files) {
        $ProbeText = & ffprobe -v error -show_entries `
            'stream=codec_type,codec_name,width,height,pix_fmt,r_frame_rate:format=duration' -of json $File.FullName
        if ($LASTEXITCODE) { throw "ffprobe failed: $($File.Name)" }
        $Probe = $ProbeText | ConvertFrom-Json
        $Video = @($Probe.streams | Where-Object codec_type -eq video)[0]
        $Width = if ($Case[3]) { 1280 } else { 2560 }
        $Height = if ($Case[3]) { 720 } else { 1440 }
        if ($Video.pix_fmt -ne $Case[4] -or $Video.width -ne $Width -or $Video.height -ne $Height) {
            throw "Unexpected output format: $($File.Name)"
        }
        if ($Case[3] -and $File.Name -like 'Replay*' -and [double]$Probe.format.duration -lt 3.75) {
            throw "Replay window unexpectedly reset: $($File.Name)"
        }
        & ffmpeg -hide_banner -loglevel error -xerror -err_detect explode -i $File.FullName `
            -map 0:v:0 -map '0:a?' -f null - 2> "$($File.FullName).decode.log"
        if ($LASTEXITCODE) { throw "Full decode failed: $($File.Name)" }
        $Results += [pscustomobject]@{
            Case = $Name
            File = $File.Name
            Bytes = $File.Length
            SHA256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            Duration = [double]$Probe.format.duration
            Width = $Video.width
            Height = $Video.height
            FPS = $Video.r_frame_rate
            PixelFormat = $Video.pix_fmt
            Decode = 'PASS'
        }
    }
    if (@(Get-ChildItem -LiteralPath $Out -Recurse -Filter 'cache.tmp' -File).Count) {
        throw "Temporary cache was not cleaned after $Name"
    }
    $Results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Manifest -Encoding utf8
    Write-Output "PASS ${Name}: $Expected files fully decoded; no cache remaining"
}
$Results | Format-Table Case, File, Duration, Width, Height, PixelFormat, Decode -AutoSize
Write-Output "VERIFIED_FILES=$($Results.Count)"
