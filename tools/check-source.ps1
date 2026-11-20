param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [string]$CMake = 'cmake'
)

$ErrorActionPreference = 'Stop'

# Работает и в Windows PowerShell 5.1 с унаследованным от pwsh PSModulePath.
function Get-SourceHash([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [Convert]::ToBase64String($sha.ComputeHash([System.IO.File]::ReadAllBytes($Path)))
    } finally {
        $sha.Dispose()
    }
}

$sourceRoot = Split-Path -Parent $PSScriptRoot
Push-Location $sourceRoot
try {
    # Общая с Windows CI проверка исходников до компиляции.
    $colors = Select-String -LiteralPath 'app/src/theme.cpp' -Pattern '#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})\b'
    if ($colors) {
        $colors | ForEach-Object { Write-Host $_ }
        throw 'Use theme color tokens instead of literal hex colors in theme.cpp.'
    }

    $before = @{}
    foreach ($file in Get-ChildItem -LiteralPath 'translations' -Filter '*.ts' -File) {
        $before[$file.FullName] = Get-SourceHash $file.FullName
    }
    & $CMake --build $BuildDir --target update_translations
    if ($LASTEXITCODE -ne 0) { throw 'Could not update translations.' }

    $changed = @()
    foreach ($file in Get-ChildItem -LiteralPath 'translations' -Filter '*.ts' -File) {
        if ($before[$file.FullName] -ne (Get-SourceHash $file.FullName)) {
            $changed += $file.Name
        }
        [xml]$translation = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
        if ($translation.SelectNodes('//message[not(translation) or translation[@type="unfinished" or @type="vanished" or @type="obsolete"]]').Count -gt 0) {
            throw "Incomplete translations in $($file.Name). Fill them in before building."
        }
    }
    if ($changed.Count -gt 0) {
        throw "Translation files were out of date and have been updated: $($changed -join ', '). Review the changes, complete any new translations, then rerun the build and include the .ts files in your commit."
    }
    Write-Host 'Source checks passed: theme tokens and translations are up to date.'
} finally {
    Pop-Location
}
