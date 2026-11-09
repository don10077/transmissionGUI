# Заполнение tgui_ru.ts русскими переводами (новые, без опоры на старые).
$ErrorActionPreference = 'Stop'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $dir 'map_a.ps1')
. (Join-Path $dir 'map_b.ps1')
$map = @{}
$mapA.GetEnumerator() | ForEach-Object { $map[$_.Key] = $_.Value }
$mapB.GetEnumerator() | ForEach-Object { $map[$_.Key] = $_.Value }

$tsPath = Join-Path $dir 'tgui_ru.ts'
$xml = [xml](Get-Content $tsPath -Raw -Encoding UTF8)
$filled = 0
$missed = @()
foreach ($c in $xml.TS.context) {
    foreach ($m in @($c.message)) {
        if ($m.translation.type -eq 'obsolete') { continue }
        $src = [string]$m.source
        if ([string]::IsNullOrWhiteSpace($src)) { continue }
        $key = $c.name + '|' + $src
        $trans = $null
        if ($ctxMap.ContainsKey($key)) { $trans = $ctxMap[$key] }
        elseif ($map.ContainsKey($src)) { $trans = $map[$src] }
        if ($null -eq $trans) { $missed += $key; continue }
        foreach ($old in @($m.GetElementsByTagName('translation'))) { $m.RemoveChild($old) }
        $node = $xml.CreateElement('translation')
        $node.InnerText = $trans
        $m.AppendChild($node)
        $filled++
    }
}
$xml.Save($tsPath)
Write-Output "filled: $filled; missed: $($missed.Count)"
$missed | ForEach-Object { Write-Output "  MISS: $_" }