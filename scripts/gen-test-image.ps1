Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap 800, 600
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(20,30,60))), 0, 0, 800, 600)
$font = New-Object System.Drawing.Font('Arial', 32)
$brush = [System.Drawing.Brushes]::White
$g.DrawString('ImageViewer M0', $font, $brush, 200, 250)
$g.DrawString('800 x 600 test', $font, $brush, 250, 320)
$bmp.Save('E:\CodingLife\LLM_ImageViewer\CodeProject\test_images\test_800x600.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "Created test image."