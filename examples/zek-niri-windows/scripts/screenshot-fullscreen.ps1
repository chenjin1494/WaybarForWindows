# 截取全部屏幕到图片文件夹
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$graphics = [System.Drawing.Graphics]::FromImage($bmp)
$graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
$dir = Join-Path ([Environment]::GetFolderPath('MyPictures')) 'Screenshots'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$file = Join-Path $dir ("Screenshot-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".png")
$bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bmp.Dispose()
Start-Process $file
