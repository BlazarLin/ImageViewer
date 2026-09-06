$files = Get-ChildItem -Path "src", "tests" -Recurse -File |
    Where-Object { $_.Extension -in ".h", ".cpp" } |
    Sort-Object FullName |
    ForEach-Object { Resolve-Path -Relative $_.FullName }
foreach ($f in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($f)
    $sampleLength = [Math]::Min(3, $bytes.Length)
    $first3 = if ($sampleLength -gt 0) {
        ($bytes[0..($sampleLength - 1)] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    } else {
        "EMPTY"
    }
    $encoding = if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        "UTF-8 BOM"
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        "UTF-16 LE BOM"
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        "UTF-16 BE BOM"
    } else {
        "NO BOM"
    }
    "{0,-64} {1,-12} first3={2}" -f $f, $encoding, $first3
}
