# 重启 waybar（通过控制管道通知运行中的实例重新加载）
$process = Get-Process waybar -ErrorAction SilentlyContinue | Select-Object -First 1
if ($process) {
    & $process.Path --reload
} else {
    $exe = Join-Path $env:LOCALAPPDATA 'Programs\Waybar\bin\waybar.exe'
    if (Test-Path $exe) {
        Start-Process $exe
    }
}
