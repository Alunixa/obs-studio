param(
    [Parameter(Mandatory)][string]$Directory,
    [string]$ReportPath
)
$ErrorActionPreference = 'Stop'
$Directory = (Resolve-Path -LiteralPath $Directory).Path
$null = Get-Command ffprobe -ErrorAction Stop

function Find-PacketWindow {
    param([string[]]$Recording, [string[]]$Replay)
    if (-not $Replay.Count) { return -1 }
    for ($start = 0; $start -le $Recording.Count - $Replay.Count; $start++) {
        if ($Recording[$start] -cne $Replay[0]) { continue }
        $offset = 1
        while ($offset -lt $Replay.Count -and $Recording[$start + $offset] -ceq $Replay[$offset]) {
            $offset++
        }
        if ($offset -eq $Replay.Count) { return $start }
    }
    return -1
}

# Reject changed, missing and reordered packets, including repeated prefixes.
if ((Find-PacketWindow @('a', 'b', 'a', 'b', 'c') @('a', 'b', 'c')) -ne 2 -or
    (Find-PacketWindow @('a', 'b', 'c') @('a', 'changed', 'c')) -ne -1 -or
    (Find-PacketWindow @('a', 'b', 'c') @('a', 'c')) -ne -1 -or
    (Find-PacketWindow @('a', 'b', 'c') @('b', 'a')) -ne -1) {
    throw 'Packet comparison self-test failed'
}

function Read-VideoPackets {
    param([string]$Path)
    $text = & ffprobe -v error -select_streams v:0 -show_packets -show_data_hash sha256 `
        -show_entries 'packet=size,flags,data_hash' -of json $Path
    if ($LASTEXITCODE -ne 0) { throw "Cannot read encoded packets: $Path" }
    $packets = @(($text | ConvertFrom-Json).packets)
    if (-not $packets.Count) { throw "No video packets: $Path" }
    foreach ($packet in $packets) {
        if ($packet.data_hash -notmatch '^SHA256:[0-9a-fA-F]{64}$') {
            throw "Missing SHA-256 packet hash: $Path"
        }
    }
    return $packets
}

$recording = @(Read-VideoPackets (Join-Path $Directory 'Recording.mkv'))
$reference = @($recording | ForEach-Object { "$($_.size)|$($_.data_hash)" })
$replays = @(Get-ChildItem -LiteralPath $Directory -Filter 'Replay*.mkv' -File | Sort-Object Name)
if (-not $replays.Count) { throw 'No replay files found' }
$results = foreach ($file in $replays) {
    $packets = @(Read-VideoPackets $file.FullName)
    if ($packets[0].flags -notmatch 'K') { throw "Replay does not start at a keyframe: $($file.Name)" }
    $signatures = @($packets | ForEach-Object { "$($_.size)|$($_.data_hash)" })
    $start = Find-PacketWindow $reference $signatures
    if ($start -lt 0) { throw "Replay packets differ from shared-encoder recording: $($file.Name)" }
    [pscustomobject]@{
        Replay = $file.Name
        VideoPackets = $packets.Count
        RecordingStartPacket = $start
        EncodedPayloads = 'IDENTICAL'
        ComparisonNegativeControls = 'PASS'
    }
}
if ($ReportPath) {
    if (Test-Path -LiteralPath $ReportPath) { throw "Report already exists: $ReportPath" }
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ReportPath -Encoding utf8
}
$results | Format-Table -AutoSize
Write-Output 'PASS every replay video packet equals a contiguous shared-encoder recording window'
