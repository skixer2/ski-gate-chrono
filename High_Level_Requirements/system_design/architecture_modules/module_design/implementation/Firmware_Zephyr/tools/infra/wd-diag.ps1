$ErrorActionPreference='SilentlyContinue'
$e=$null
[System.Management.Automation.PSParser]::Tokenize((Get-Content 'C:\sgc\tools\infra\node-watchdog.ps1' -Raw),[ref]$e)|Out-Null
Write-Output ('SYNTAX_ERRORS=' + $e.Count)
$raw = Get-Content 'C:\sgc\tools\infra\node-watchdog.ps1' -Raw
$anchors = 'http.server','tunnel-run.cmd','RedirectStandardOutput','USERPROFILE = $USER_DIR','node-watchdog.ps1','Test-GatewayViaTunnel','DEAD_CYCLES_BEFORE_RECYCLE','v2.7: starting','FORCE_MIN','Get-NewestLogWrite','Ensure-Web','http.log'
foreach ($a in $anchors) {
    if ($raw.Contains($a)) { Write-Output ('ANCHOR_OK ' + $a) } else { Write-Output ('ANCHOR_MISSING ' + $a) }
}
$xml = Get-Content 'C:\sgc\tools\infra\SGC-WatchdogBoot-task.v2.xml' -Raw
if ($xml -match 'node-watchdog\.ps1') { Write-Output 'XML_REFERENCES_PS1_OK' } else { Write-Output 'XML_PS1_MISSING' }
if ($xml -match '<MultipleInstancesPolicy>([A-Za-z]+)</MultipleInstancesPolicy>') { Write-Output ('XML_MULTI_INSTANCE=' + $Matches[1]) }