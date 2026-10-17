# 发送 Win 键（打开开始菜单）
$ws = New-Object -ComObject WScript.Shell
$ws.SendKeys('^{ESC}')
