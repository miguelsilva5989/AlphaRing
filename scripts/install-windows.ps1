param(
    [string]$MccPath,
    [string]$DllPath,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

function Resolve-Win64([string]$BasePath) {
    if ([string]::IsNullOrWhiteSpace($BasePath)) { return $null }
    $BasePath = $BasePath.Trim('"').TrimEnd('\', '/')
    $Candidates = @(
        $BasePath,
        (Join-Path $BasePath "MCC\Binaries\Win64"),
        (Join-Path $BasePath "mcc\binaries\win64")
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath (Join-Path $Candidate "MCC-Win64-Shipping.exe") -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }
    return $null
}

function Add-UniquePath([System.Collections.Generic.List[string]]$List, [string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    $Path = $Path.Trim('"').Replace('\\', '\').TrimEnd('\', '/')
    if ((Test-Path -LiteralPath (Join-Path $Path "steamapps")) -and -not $List.Contains($Path)) {
        $List.Add($Path)
    }
}

$SteamRoots = [System.Collections.Generic.List[string]]::new()
if (${env:ProgramFiles(x86)}) {
    Add-UniquePath $SteamRoots (Join-Path ${env:ProgramFiles(x86)} "Steam")
}
if ($env:ProgramFiles) {
    Add-UniquePath $SteamRoots (Join-Path $env:ProgramFiles "Steam")
}

foreach ($RegistryPath in @(
    "HKCU:\Software\Valve\Steam",
    "HKLM:\Software\WOW6432Node\Valve\Steam",
    "HKLM:\Software\Valve\Steam"
)) {
    try {
        $SteamPath = (Get-ItemProperty -Path $RegistryPath -ErrorAction Stop).SteamPath
        Add-UniquePath $SteamRoots $SteamPath
    } catch {}
}

foreach ($Root in @($SteamRoots)) {
    $Vdf = Join-Path $Root "steamapps\libraryfolders.vdf"
    if (-not (Test-Path -LiteralPath $Vdf)) { continue }
    foreach ($Line in Get-Content -LiteralPath $Vdf) {
        if ($Line -match '"path"\s+"([^"]+)"') {
            Add-UniquePath $SteamRoots $Matches[1]
        }
    }
}

$Win64 = $null
if ($MccPath) {
    $Win64 = Resolve-Win64 $MccPath
    if (-not $Win64) {
        throw "Could not find MCC\Binaries\Win64 below: $MccPath"
    }
} else {
    $Installs = [System.Collections.Generic.List[string]]::new()
    foreach ($Root in $SteamRoots) {
        $Candidate = Join-Path $Root "steamapps\common\Halo The Master Chief Collection\MCC\Binaries\Win64"
        if ((Test-Path -LiteralPath (Join-Path $Candidate "MCC-Win64-Shipping.exe")) -and -not $Installs.Contains($Candidate)) {
            $Installs.Add($Candidate)
        }
    }

    if ($Installs.Count -eq 1) {
        $Win64 = $Installs[0]
    } elseif ($Installs.Count -gt 1) {
        Write-Host "Multiple MCC installations found:"
        for ($Index = 0; $Index -lt $Installs.Count; $Index++) {
            Write-Host ("  {0}) {1}" -f ($Index + 1), $Installs[$Index])
        }
        $Selection = [int](Read-Host "Choose installation [1-$($Installs.Count)]")
        if ($Selection -lt 1 -or $Selection -gt $Installs.Count) { throw "Invalid selection" }
        $Win64 = $Installs[$Selection - 1]
    } else {
        $EnteredPath = Read-Host "MCC game directory"
        $Win64 = Resolve-Win64 $EnteredPath
        if (-not $Win64) { throw "Could not find MCC\Binaries\Win64 below that directory" }
    }
}

$Target = Join-Path $Win64 "WTSAPI32.dll"
$Backup = "$Target.alpharing-backup"

if ($Uninstall) {
    if (Test-Path -LiteralPath $Backup) {
        Move-Item -LiteralPath $Backup -Destination $Target -Force
        Write-Host "Restored previous DLL: $Target"
    } elseif (Test-Path -LiteralPath $Target) {
        Remove-Item -LiteralPath $Target -Force
        Write-Host "Removed: $Target"
    } else {
        Write-Host "AlphaRing DLL is not installed at: $Target"
    }
    exit 0
}

if (-not $DllPath) {
    foreach ($Candidate in @(
        (Join-Path $ScriptDir "..\WTSAPI32.dll"),
        (Join-Path $ScriptDir "WTSAPI32.dll"),
        (Join-Path $ScriptDir "..\build\Release\WTSAPI32.dll"),
        (Join-Path $ScriptDir "..\build-mingw\WTSAPI32.dll")
    )) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            $DllPath = $Candidate
            break
        }
    }
}
if (-not $DllPath -or -not (Test-Path -LiteralPath $DllPath -PathType Leaf)) {
    throw "WTSAPI32.dll was not found. Pass -DllPath C:\path\to\WTSAPI32.dll"
}

if ((Test-Path -LiteralPath $Target) -and -not (Test-Path -LiteralPath $Backup)) {
    Copy-Item -LiteralPath $Target -Destination $Backup
    Write-Host "Backed up existing DLL: $Backup"
}
Copy-Item -LiteralPath $DllPath -Destination $Target -Force

$GameRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $Win64))
$ResourceSource = $null
foreach ($Candidate in @(
    (Join-Path $ScriptDir "..\res"),
    (Join-Path $ScriptDir "..\alpha_ring"),
    (Join-Path $ScriptDir "res"),
    (Join-Path $ScriptDir "alpha_ring")
)) {
    if (Test-Path -LiteralPath $Candidate -PathType Container) {
        $ResourceSource = $Candidate
        break
    }
}
if ($ResourceSource) {
    $ResourceTarget = Join-Path $GameRoot "alpha_ring"
    New-Item -ItemType Directory -Path $ResourceTarget -Force | Out-Null
    Copy-Item -Path (Join-Path $ResourceSource "*") -Destination $ResourceTarget -Recurse -Force
}

Write-Host ""
Write-Host "Installed AlphaRing to:"
Write-Host "  $Target"
Write-Host ""
Write-Host "Launch MCC using the anti-cheat-disabled option."
