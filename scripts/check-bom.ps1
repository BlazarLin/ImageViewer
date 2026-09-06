$files = @(
    "src\app\Application.h",
    "src\app\Application.cpp",
    "src\app\MainWindow.h",
    "src\app\MainWindow.cpp",
    "src\ui\ImageView.h",
    "src\ui\ImageView.cpp",
    "src\core\loader\ImageLoader.h",
    "src\core\loader\ImageLoader.cpp",
    "src\util\ElapsedLog.h",
    "src\main.cpp"
)
foreach ($f in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($f)
    $first3 = ($bytes[0..2] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    $encoding = if ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        "UTF-8 BOM"
    } elseif ($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        "UTF-16 LE BOM"
    } elseif ($bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        "UTF-16 BE BOM"
    } else {
        "NO BOM"
    }
    "{0,-40} {1,-12} first3={2}" -f $f, $encoding, $first3
}