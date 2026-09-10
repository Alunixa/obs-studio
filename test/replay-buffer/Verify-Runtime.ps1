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
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, ExactSpelling=true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    private delegate bool Startup([MarshalAs(UnmanagedType.LPUTF8Str)] string locale, IntPtr config, IntPtr profiler);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void Shutdown();
    public static bool InitializeCore(IntPtr module) {
        var start = (Startup)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module, "obs_startup"), typeof(Startup));
        return start("en-US", IntPtr.Zero, IntPtr.Zero);
    }
    public static void ShutdownCore(IntPtr module) {
        var stop = (Shutdown)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module, "obs_shutdown"), typeof(Shutdown));
        stop();
    }
}
'@

$Cookie = [ObsRuntimeLoader]::AddDllDirectory($Bin)
if ($Cookie -eq [IntPtr]::Zero) {
    throw "Cannot register runtime directory: $Bin"
}
$Core = [IntPtr]::Zero
$Initialized = $false
$Modules = [System.Collections.Generic.List[IntPtr]]::new()
try {
    foreach ($Name in @('obs64.exe', 'obs.dll', 'obs-nvenc-test.exe', 'obs-ffmpeg-mux.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Bin $Name))) {
            throw "Missing required runtime file: $Name"
        }
    }
    $Core = [ObsRuntimeLoader]::LoadLibraryEx((Join-Path $Bin 'obs.dll'), [IntPtr]::Zero, 0x1100)
    if ($Core -eq [IntPtr]::Zero) { throw 'Cannot load libobs' }
    # WebRTC's static user-agent initializer calls obs_get_locale(). Match the
    # real application lifecycle rather than reporting an artificial DLL error.
    $Initialized = [ObsRuntimeLoader]::InitializeCore($Core)
    if (-not $Initialized) { throw 'Cannot initialize libobs for dependency verification' }
    foreach ($Name in @('obs-ffmpeg.dll', 'win-dshow.dll', 'obs-nvenc.dll', 'obs-webrtc.dll', 'obs-websocket.dll')) {
        $Path = Join-Path $Plugins $Name
        # Resolve imports, rather than only checking that the top-level DLL exists.
        $Module = [ObsRuntimeLoader]::LoadLibraryEx($Path, [IntPtr]::Zero, 0x1100)
        if ($Module -eq [IntPtr]::Zero) {
            $Code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "Cannot load $Name and its dependencies (Win32 error $Code)"
        }
        $Modules.Add($Module)
        Write-Output "PASS dependency load: $Name"
    }
} finally {
    for ($Index = $Modules.Count - 1; $Index -ge 0; $Index--) {
        [void][ObsRuntimeLoader]::FreeLibrary($Modules[$Index])
    }
    if ($Initialized) { [ObsRuntimeLoader]::ShutdownCore($Core) }
    if ($Core -ne [IntPtr]::Zero) { [void][ObsRuntimeLoader]::FreeLibrary($Core) }
    [void][ObsRuntimeLoader]::RemoveDllDirectory($Cookie)
}
Write-Output 'ALL CRITICAL OBS RUNTIME DEPENDENCIES LOADED'
