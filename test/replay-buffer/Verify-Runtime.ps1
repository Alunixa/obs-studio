[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Root
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path
$Bin = Join-Path $Root 'bin/64bit'
$Plugins = Join-Path $Root 'obs-plugins/64bit'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ObsRuntimeLoader {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr AddDllDirectory(string directory);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool RemoveDllDirectory(IntPtr cookie);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool FreeLibrary(IntPtr module);
}
'@

$Cookie = [ObsRuntimeLoader]::AddDllDirectory($Bin)
if ($Cookie -eq [IntPtr]::Zero) {
    throw "Cannot register runtime directory: $Bin"
}
try {
    foreach ($Name in @('obs64.exe', 'obs.dll', 'obs-nvenc-test.exe', 'obs-ffmpeg-mux.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Bin $Name))) {
            throw "Missing required runtime file: $Name"
        }
    }
    foreach ($Name in @('obs-ffmpeg.dll', 'win-dshow.dll', 'obs-nvenc.dll', 'obs-webrtc.dll', 'obs-websocket.dll')) {
        $Path = Join-Path $Plugins $Name
        # Resolve imports, rather than only checking that the top-level DLL exists.
        $Module = [ObsRuntimeLoader]::LoadLibraryEx($Path, [IntPtr]::Zero, 0x1100)
        if ($Module -eq [IntPtr]::Zero) {
            $Code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "Cannot load $Name and its dependencies (Win32 error $Code)"
        }
        [void][ObsRuntimeLoader]::FreeLibrary($Module)
        Write-Output "PASS dependency load: $Name"
    }
} finally {
    [void][ObsRuntimeLoader]::RemoveDllDirectory($Cookie)
}
Write-Output 'ALL CRITICAL OBS RUNTIME DEPENDENCIES LOADED'
