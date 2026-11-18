param(
    [ValidateSet('DEBUG', 'RELEASE')]
    [string]$Target = 'DEBUG'
)
$ErrorActionPreference = 'Stop'
$LinuxRoot = (& wsl.exe -d Ubuntu-24.04 --exec wslpath -u $PSScriptRoot).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the workspace path in Ubuntu-24.04.' }
& wsl.exe -d Ubuntu-24.04 --exec bash "$LinuxRoot/scripts/run-iso.sh" $Target
if ($LASTEXITCODE -ne 0) { throw "QEMU failed (exit $LASTEXITCODE)." }
