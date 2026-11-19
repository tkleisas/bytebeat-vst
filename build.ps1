# Build Bytebeat Machine and Bytebeat Drums for MPC/desktop, plus the Windows VST3.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRepo = if ($env:SDK_REPO) { $env:SDK_REPO } else { "https://github.com/inmusic-software/mpc-plugin-builder-sdk.git" }
$SdkCommit = if ($env:SDK_COMMIT) { $env:SDK_COMMIT } else { "a5949cf6fe2cbfb95bcbe0ddf1f7ef555e71f2be" }
$Python = if ($env:PYTHON) { $env:PYTHON } else { "python" }
$SdkDir = if ($env:SDK_DIR) { $env:SDK_DIR } else { Join-Path $Root ".sdk" }
$Dist = Join-Path $Root "dist"

$MachineName = "Bytebeat Machine"
$MachineSlug = "bytebeat-machine"
$MachineVersion = "0.3.1"
$DrumsName = "Bytebeat Drums"
$DrumsSlug = "bytebeat-drums"
$DrumsVersion = "0.1.0"

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

New-Item -ItemType Directory -Force -Path $Dist | Out-Null

$ZigArgs = @()
if ($env:ZIG) { $ZigArgs = @("--zig", $env:ZIG) }
$BuildScript = Join-Path $SdkDir "tools/build.py"
$HostSim = Join-Path $SdkDir ".sdk-tools/host_sim.exe"
$CoreHeaders = @(
    (Join-Path $Root "mpc/src/BytebeatCompile.h"),
    (Join-Path $Root "mpc/src/BytebeatVM.h"),
    (Join-Path $Root "mpc/src/BytebeatEngine.h"),
    (Join-Path $Root "mpc/src/FormulaLibrary.h")
)

function Build-MpcPlugin {
    param(
        [string]$Src,
        [string]$Name,
        [string]$Slug,
        [string]$Notes,
        [string]$Version
    )

    Write-Host ""
    Write-Host "== $Name =="

    $PluginDir = Join-Path $SdkDir "plugins/$Name"
    if (Test-Path $PluginDir) { Remove-Item -Recurse -Force $PluginDir }
    New-Item -ItemType Directory -Force -Path $PluginDir | Out-Null
    Copy-Item -Recurse -Force -Path (Join-Path $Src "/*") -Destination $PluginDir
    Copy-Item -Force -Path $CoreHeaders -Destination (Join-Path $PluginDir "src")

    Write-Host "-- building MPC (native + ARM32 + ARM64)"
    Invoke-Checked $Python (@($BuildScript, $PluginDir, "--target", "all") + $ZigArgs)

    Write-Host "-- building desktop VST2"
    Invoke-Checked $Python (@($BuildScript, $PluginDir, "--target", "desktop") + $ZigArgs)

    Write-Host "-- running host simulator test"
    $Native = Join-Path $PluginDir "build/native/$Slug.dll"
    if ((Test-Path $HostSim) -and (Test-Path $Native)) {
        $TestWav = Join-Path $Dist ".host-sim-$Slug.wav"
        Invoke-Checked $HostSim @($Native, "test", "--seconds", "3", "--notes", $Notes, "--wav", $TestWav)
        Remove-Item -Force $TestWav -ErrorAction SilentlyContinue
    } else {
        Write-Host "note: host_sim or native DLL not found; skipping the simulator test"
    }

    Write-Host "-- packaging MPC release asset"
    Invoke-Checked $Python @(
        (Join-Path $SdkDir "tools/package.py"),
        $PluginDir,
        "--out", $Dist
    )

    Write-Host "-- packaging desktop VST2"
    $Desktop = Join-Path $PluginDir "build/desktop/$Name"
    if (Test-Path $Desktop) {
        $DistDesktop = Join-Path $Dist "$Slug-desktop-VST2"
        if (Test-Path $DistDesktop) { Remove-Item -Recurse -Force $DistDesktop }
        Copy-Item -Recurse $Desktop $DistDesktop
        Copy-Item (Join-Path $Root "LICENSE") $Dist
        Copy-Item (Join-Path $Root "NOTICE.md") $Dist
        Compress-Archive -Force -Path $DistDesktop, (Join-Path $Root "LICENSE"), (Join-Path $Root "NOTICE.md") `
            -DestinationPath (Join-Path $Dist "$Slug-$Version-windows-vst2.zip")
    }
}

Build-MpcPlugin -Src (Join-Path $Root "mpc") -Name $MachineName -Slug $MachineSlug `
    -Notes "60,64,67" -Version $MachineVersion
Build-MpcPlugin -Src (Join-Path $Root "drums/mpc") -Name $DrumsName -Slug $DrumsSlug `
    -Notes "36,38,40" -Version $DrumsVersion

Write-Host ""
Write-Host "== Bytebeat Machine VST3 =="
if (-not (Test-Path (Join-Path $Root "third_party/JUCE/CMakeLists.txt"))) {
    Invoke-Checked "git" @("-C", $Root, "submodule", "update", "--init", "--recursive")
}
$BuildDir = Join-Path $Root "vst3/build-windows"
Invoke-Checked "cmake" @("-S", (Join-Path $Root "vst3"), "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=Release")
Invoke-Checked "cmake" @("--build", $BuildDir, "--config", "Release", "--parallel")

$Bundle = Join-Path $BuildDir "BytebeatMachine_artefacts/Release/VST3/$MachineName.vst3"
if (-not (Test-Path $Bundle)) { throw "VST3 bundle not found at $Bundle" }

$DistVst3 = Join-Path $Dist "$MachineName.vst3"
if (Test-Path $DistVst3) { Remove-Item -Recurse -Force $DistVst3 }
Copy-Item -Recurse $Bundle $DistVst3
Copy-Item (Join-Path $Root "LICENSE") $Dist
Copy-Item (Join-Path $Root "NOTICE.md") $Dist
Copy-Item (Join-Path $Root "LICENSES/QuickJS-NG-MIT.txt") $Dist
Compress-Archive -Force -Path $DistVst3, (Join-Path $Root "LICENSE"), (Join-Path $Root "NOTICE.md"), `
    (Join-Path $Root "LICENSES/QuickJS-NG-MIT.txt") `
    -DestinationPath (Join-Path $Dist "$MachineSlug-$MachineVersion-windows-vst3.zip")

Write-Host ""
Write-Host "== Bytebeat Drums VST3 =="
$DrumsBuildDir = Join-Path $Root "drums/vst3/build-windows"
Invoke-Checked "cmake" @("-S", (Join-Path $Root "drums/vst3"), "-B", $DrumsBuildDir,
    "-DCMAKE_BUILD_TYPE=Release")
Invoke-Checked "cmake" @("--build", $DrumsBuildDir, "--config", "Release", "--parallel")

$DrumsBundle = Join-Path $DrumsBuildDir "BytebeatDrums_artefacts/Release/VST3/$DrumsName.vst3"
if (-not (Test-Path $DrumsBundle)) { throw "drum VST3 bundle not found at $DrumsBundle" }

$DrumsDistVst3 = Join-Path $Dist "$DrumsName.vst3"
if (Test-Path $DrumsDistVst3) { Remove-Item -Recurse -Force $DrumsDistVst3 }
Copy-Item -Recurse $DrumsBundle $DrumsDistVst3
Copy-Item (Join-Path $Root "LICENSE") $Dist
Copy-Item (Join-Path $Root "NOTICE.md") $Dist
Compress-Archive -Force -Path $DrumsDistVst3, (Join-Path $Root "LICENSE"), (Join-Path $Root "NOTICE.md") `
    -DestinationPath (Join-Path $Dist "$DrumsSlug-$DrumsVersion-windows-vst3.zip")

Write-Host ""
Write-Host "Done. Release assets:"
Get-ChildItem $Dist -Filter "*.zip" | Format-Table Name, Length
