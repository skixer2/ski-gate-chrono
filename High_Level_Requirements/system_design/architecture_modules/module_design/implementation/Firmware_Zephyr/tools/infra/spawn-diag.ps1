# spawn-diag.ps1 - 0xC0000142 diagnosis sweep (2026-11-13, read-only + spawn matrix)
$ErrorActionPreference='SilentlyContinue'
Write-Output ('USER=' + $env:USERNAME + ' SESSION=' + (Get-Process -Id $PID).SessionId)
# Suspect 1: AppInit_DLLs injection (both registry views)
foreach ($k in 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows','HKLM:\SOFTWARE\Wow6432Node\Microsoft\Windows NT\CurrentVersion\Windows') {
  $p = Get-ItemProperty $k
  Write-Output ('APPINIT view=[' + $k + '] DLLs=[' + $p.AppInit_DLLs + '] Load=' + $p.LoadAppInit_DLLs)
}
# Suspect 2: desktop heap (SharedSection - 2nd value = service/session-0 heap in KB)
$ss = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\SubSystems').Windows
Write-Output ('SHAREDSECTION ' + $ss)
# Suspect 3: event-log traces (24h, errors mentioning dll init / sidebyside)
$since = (Get-Date).AddHours(-24)
foreach ($log in 'Application','System') {
  Get-WinEvent -FilterHashtable @{LogName=$log; Level=1,2,3; StartTime=$since} -MaxEvents 300 |
    Where-Object { $_.Message -match 'c0000142|SideBySide|DLL' } |
    Select-Object -First 5 | ForEach-Object { Write-Output ('EVT ' + $log + ' ' + $_.TimeCreated.ToString('HH:mm') + ' id=' + $_.Id + ' :: ' + (($_.Message -split "`n")[0]).Substring(0,[Math]::Min(120,(($_.Message -split "`n")[0]).Length))) }
}
# Suspect 4: spawn matrix in THIS context (the one that died 0xC0000142)
$f1 = 'C:\Users\v17ni\spawn_wmi_test.txt'
if (Test-Path $f1) { Remove-Item $f1 -Force }
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = 'cmd /c echo WMI_OK > C:\Users\v17ni\spawn_wmi_test.txt' }
Write-Output ('WMI_CREATE_RC=' + $r.ReturnValue + ' pid=' + $r.ProcessId)
Start-Sleep -Seconds 2
Write-Output ('WMI_FILE_ALIVE=' + (Test-Path $f1))
Start-Process cmd -ArgumentList '/c','echo SP_OK > C:\Users\v17ni\spawn_sp_test.txt' -WindowStyle Hidden -Wait
Write-Output ('STARTPROCESS_FILE_ALIVE=' + (Test-Path 'C:\Users\v17ni\spawn_sp_test.txt'))
Write-Output 'DIAG_DONE'
