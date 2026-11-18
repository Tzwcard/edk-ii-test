param(
    [ValidateSet('DEBUG', 'RELEASE')]
    [string]$Target = 'DEBUG'
)
$ErrorActionPreference = 'Stop'
$LinuxRoot = (& wsl.exe -d Ubuntu-24.04 --exec wslpath -u $PSScriptRoot).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the workspace path in Ubuntu-24.04.' }
& wsl.exe -d Ubuntu-24.04 --exec bash "$LinuxRoot/scripts/build.sh" $Target
if ($LASTEXITCODE -ne 0) { throw "EDK II build failed (exit $LASTEXITCODE)." }
