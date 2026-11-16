param(
    [string]$PortableZip = "$PSScriptRoot/../build/packages/transmissiongui-windows-x64.zip",
    [string]$MakeNsis = 'makensis.exe',
    [string]$OutputDir = "$PSScriptRoot/../build/packages"
)

$ErrorActionPreference = 'Stop'
$project = Get-Content -LiteralPath "$PSScriptRoot/../CMakeLists.txt" -Raw
if ($project -notmatch '(?m)^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$') {
    throw 'Could not read the project version'
}
$version = $Matches[1]
$source = (Resolve-Path -LiteralPath $PortableZip).Path
$output = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName
$setup = Join-Path $output "transmissiongui-$version-windows-x64-setup.exe"
$stage = (New-Item -ItemType Directory -Path (Join-Path $output "installer-stage-$([guid]::NewGuid())")).FullName

try {
    $payload = Join-Path $stage 'payload'
    Expand-Archive -LiteralPath $source -DestinationPath $payload
    foreach ($required in 'transmissiongui.exe', 'LICENSE', 'platforms/qwindows.dll') {
        if (!(Test-Path -LiteralPath "$payload/$required" -PathType Leaf)) {
            throw "Portable archive is missing $required"
        }
    }

    # Include only files shipped in the ZIP. User-added files and settings survive uninstall.
    $commands = foreach ($file in Get-ChildItem -LiteralPath $payload -File -Recurse) {
        $relative = $file.FullName.Substring($payload.Length + 1)
        if ($relative -notmatch '^[a-zA-Z0-9_.+\\-]+$' -or $relative -ieq 'uninstall.exe') {
            throw "Unsupported file name in portable archive: $relative"
        }
        if ($relative -ine 'transmissiongui.exe') {
            'Delete "$INSTDIR\{0}"' -f $relative
        }
    }
    $commands += foreach ($dir in Get-ChildItem -LiteralPath $payload -Directory -Recurse |
        Sort-Object { $_.FullName.Length } -Descending) {
        $relative = $dir.FullName.Substring($payload.Length + 1)
        if ($relative -notmatch '^[a-zA-Z0-9_.\\-]+$') {
            throw "Unsupported directory name in portable archive: $relative"
        }
        'RMDir "$INSTDIR\{0}"' -f $relative
    }
    $manifest = Join-Path $stage 'payload-uninstall.nsh'
    Set-Content -LiteralPath $manifest -Value $commands -Encoding ASCII
    $script = (Resolve-Path -LiteralPath "$PSScriptRoot/../packaging/windows/transmissiongui.nsi").Path
    & $MakeNsis '/V2' "/DVERSION=$version" "/DPAYLOAD=$payload" "/DOUTPUT=$setup" "/DMANIFEST=$manifest" $script
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $setup -PathType Leaf)) {
        throw "makensis failed to build the installer (exit code $LASTEXITCODE)"
    }
    Write-Output "Windows installer $setup"
} finally {
    $outputPrefix = $output.TrimEnd('\') + [IO.Path]::DirectorySeparatorChar
    if ($stage.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
