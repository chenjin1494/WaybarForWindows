# 切换蓝牙无线电开关（Windows 10/11）
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]
function Await($operation, $type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
    $task.Wait(3000) | Out-Null
    $task.Result
}
[void][Windows.Devices.Radios.Radio, Windows.Devices.Radios, ContentType = WindowsRuntime]
$radios = Await ([Windows.Devices.Radios.Radio]::GetRadiosAsync()) ([System.Collections.Generic.IReadOnlyList[Windows.Devices.Radios.Radio]])
$bluetooth = $radios | Where-Object { $_.Kind -eq 'Bluetooth' } | Select-Object -First 1
if ($null -eq $bluetooth) { exit 0 }
$target = if ($bluetooth.State -eq [Windows.Devices.Radios.RadioState]::On) {
    [Windows.Devices.Radios.RadioState]::Off
} else {
    [Windows.Devices.Radios.RadioState]::On
}
Await ($bluetooth.SetStateAsync($target)) ([Windows.Devices.Radios.RadioAccessStatus]) | Out-Null
