# Build Bytebeat Machine for MPC, desktop VST2 and Windows VST3.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRepo = if ($env:SDK_REPO) { $env:SDK_REPO } else { "https://github.com/inmusic-software/mpc-plugin-builder-sdk.git" }
$SdkCommit = if ($env:SDK_COMMIT) { $env:SDK_COMMIT } else { "a5949cf6fe2cbfb95bcbe0ddf1f7ef555e71f2be" }
$Python = if ($env:PYTHON) { $env:PYTHON } else { "python" }
$SdkDir = if ($env:SDK_DIR) { $env:SDK_DIR } else { Join-Path $Root ".sdk" }
$PluginName = "Bytebeat Machine"
$Version = "0.1.0"

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

if (-not (Test-Path (Join-Path $SdkDir ".git"))) {
    Write-Host "-- cloning MPC Plugin SDK"
    Invoke-Checked "git" @("clone", $SdkRepo, $SdkDir)
}
& git -C $SdkDir fetch --quiet origin $SdkCommit
& git -C $SdkDir checkout --quiet $SdkCommit
if ($LASTEXITCODE -ne 0) { throw "could not check out SDK commit $SdkCommit" }

$PluginDir = Join-Path $SdkDir "plugins/$PluginName"
if (Test-Path $PluginDir) { Remove-Item -Recurse -Force $PluginDir }
New-Item -ItemType Directory -Force -Path (Join-Path $SdkDir "plugins") | Out-Null
Copy-Item -Recurse (Join-Path $Root "mpc") (Join-Path $SdkDir "plugins")

$ZigArgs = @()
if ($env:ZIG) { $ZigArgs = @("--zig", $env:ZIG) }
$BuildScript = Join-Path $SdkDir "tools/build.py"

Write-Host "-- building MPC (native + ARM32 + ARM64)"
Invoke-Checked $Python (@($BuildScript, $PluginDir, "--target", "all") + $ZigArgs)

Write-Host "-- building desktop VST2"
Invoke-Checked $Python (@($BuildScript, $PluginDir, "--target", "desktop") + $ZigArgs)

Write-Host "-- running host simulator test"
$HostSim = Join-Path $SdkDir ".sdk-tools/host_sim.exe"
$Native = Join-Path $PluginDir "build/native/bytebeat-machine.dll"
if ((Test-Path $HostSim) -and (Test-Path $Native)) {
    $TestWav = Join-Path $Root "dist/.host-sim-test.wav"
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "dist") | Out-Null
    Invoke-Checked $HostSim @($Native, "test", "--seconds", "3", "--notes", "60,64,67", "--wav", $TestWav)
    Remove-Item -Force $TestWav -ErrorAction SilentlyContinue
} else {
    Write-Host "note: host_sim or native DLL not found; skipping the simulator test"
}

Write-Host "-- packaging MPC release asset"
Invoke-Checked $Python @(
    (Join-Path $SdkDir "tools/package.py"),
    $PluginDir,
    "--out", (Join-Path $Root "dist")
)

Write-Host "-- packaging desktop VST2"
$Desktop = Join-Path $PluginDir "build/desktop/$PluginName"
if (Test-Path $Desktop) {
    $DistDesktop = Join-Path $Root "dist/desktop-VST2"
    if (Test-Path $DistDesktop) { Remove-Item -Recurse -Force $DistDesktop }
    Copy-Item -Recurse $Desktop $DistDesktop
    Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Root "dist")
    Copy-Item (Join-Path $Root "NOTICE.md") (Join-Path $Root "dist")
    Compress-Archive -Force -Path $DistDesktop, (Join-Path $Root "LICENSE"), (Join-Path $Root "NOTICE.md") `
        -DestinationPath (Join-Path $Root "dist/bytebeat-machine-$Version-windows-vst2.zip")
}

Write-Host "-- building VST3"
if (-not (Test-Path (Join-Path $Root "third_party/JUCE/CMakeLists.txt"))) {
    Invoke-Checked "git" @("-C", $Root, "submodule", "update", "--init", "--recursive")
}
$BuildDir = Join-Path $Root "vst3/build-windows"
Invoke-Checked "cmake" @("-S", (Join-Path $Root "vst3"), "-B", $BuildDir,
    "-G", "Visual Studio 17 2022", "-A", "x64", "-DCMAKE_BUILD_TYPE=Release")
Invoke-Checked "cmake" @("--build", $BuildDir, "--config", "Release", "--parallel")

$Bundle = Join-Path $BuildDir "BytebeatMachine_artefacts/Release/VST3/Bytebeat Machine.vst3"
if (-not (Test-Path $Bundle)) { throw "VST3 bundle not found at $Bundle" }

$DistVst3 = Join-Path $Root "dist/Bytebeat Machine.vst3"
if (Test-Path $DistVst3) { Remove-Item -Recurse -Force $DistVst3 }
Copy-Item -Recurse $Bundle $DistVst3
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Root "dist")
Copy-Item (Join-Path $Root "NOTICE.md") (Join-Path $Root "dist")
Compress-Archive -Force -Path $DistVst3, (Join-Path $Root "LICENSE"), (Join-Path $Root "NOTICE.md") `
    -DestinationPath (Join-Path $Root "dist/bytebeat-machine-$Version-windows-vst3.zip")

Write-Host ""
Write-Host "Done. Release assets:"
Get-ChildItem (Join-Path $Root "dist") -Filter "*.zip" | Format-Table Name, Length
