param(
    [string]$BuildDir = "$PSScriptRoot/../build/win-release",
    [string]$QtBin = 'D:/Install/MSYS2/mingw64/bin',
    [string]$OutputDir = "$PSScriptRoot/../build/packages"
)

$ErrorActionPreference = 'Stop'
$qtTools = (Resolve-Path -LiteralPath $QtBin).Path
$app = (Resolve-Path -LiteralPath "$BuildDir/app/transmissiongui.exe").Path
$output = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName
$stage = (New-Item -ItemType Directory -Path (Join-Path $output "stage-$([guid]::NewGuid())")).FullName
$archive = Join-Path $output 'transmissiongui-windows-x64.zip'
$oldPath = $env:Path

try {
    $env:Path = "$qtTools;$oldPath"
    Copy-Item -LiteralPath $app -Destination $stage
    & "$qtTools/windeployqt.exe" --release --no-translations --no-compiler-runtime --force-openssl --include-plugins qwindows,qoffscreen "$stage/transmissiongui.exe"
    if ($LASTEXITCODE -ne 0) { throw "windeployqt exited with code $LASTEXITCODE" }

    # Qt loads OpenSSL dynamically. Include it explicitly and follow PE imports
    # for all binaries, including plugins and non-Qt MinGW libraries.
    foreach ($name in 'libssl-3-x64.dll', 'libcrypto-3-x64.dll') {
        Copy-Item -LiteralPath "$qtTools/$name" -Destination $stage
    }
    $pending = [Collections.Generic.Queue[string]]::new()
    Get-ChildItem -LiteralPath $stage -Recurse -File |
        Where-Object Extension -in '.exe', '.dll' |
        ForEach-Object { $pending.Enqueue($_.FullName) }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($pending.Count -gt 0) {
        $binary = $pending.Dequeue()
        if (!$seen.Add($binary)) { continue }
        $imports = & "$qtTools/objdump.exe" -p $binary
        if ($LASTEXITCODE -ne 0) { throw "objdump could not read $binary" }
        foreach ($line in $imports) {
            if ($line -notmatch '^\s+DLL Name:\s+(\S+)') { continue }
            $dll = $Matches[1]
            if (Test-Path -LiteralPath "$stage/$dll") { continue }
            if (Test-Path -LiteralPath "$qtTools/$dll") {
                Copy-Item -LiteralPath "$qtTools/$dll" -Destination $stage
                $pending.Enqueue("$stage/$dll")
            } elseif ($dll -notmatch '^(api-ms-|ext-ms-)' -and
                      !(Test-Path -LiteralPath "$env:SystemRoot/System32/$dll")) {
                throw "Missing runtime dependency $dll for $binary"
            }
        }
    }
    Compress-Archive -Path "$stage/*" -DestinationPath $archive -Force
    Write-Output "Portable archive $archive"
} finally {
    $env:Path = $oldPath
    if ($stage.StartsWith($output + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
