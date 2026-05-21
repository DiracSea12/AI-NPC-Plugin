param(
    [string]$RunId
)

$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "TestResult.ps1")

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$context = New-AINpcTestRunContext -Layer "static" -RunId $RunId
$startTime = (Get-Date).ToUniversalTime()
$verifyScripts = @(Get-ChildItem -Path $scriptRoot -Filter "verify-*.ps1" -File | Sort-Object Name)
$checks = @()
$failures = @()
$status = "PASS"
$commandArgs = @("-File", $MyInvocation.MyCommand.Path)
if (-not [string]::IsNullOrWhiteSpace($RunId)) { $commandArgs += @("-RunId", $RunId) }
$command = New-AINpcCommandInfo -Executable "pwsh" -Arguments $commandArgs -WorkingDirectory (Get-AINpcRepoRoot)

if ($verifyScripts.Count -eq 0) {
    $status = "BLOCKED"
    $failures += [ordered]@{
        id = "static-discovery"
        message = "No static verification scripts found in $scriptRoot"
        artifact = $null
    }
}
else {
    foreach ($script in $verifyScripts) {
        Write-Host "Running static verification: $($script.Name)"
        $checkStart = (Get-Date).ToUniversalTime()
        $checkStatus = "PASS"
        $exitCode = 0
        $message = ""
        try {
            $global:LASTEXITCODE = 0
            & $script.FullName
            if (-not $?) {
                $checkStatus = "FAIL"
                $message = "$($script.Name) failed"
            }
            elseif ($LASTEXITCODE -ne 0) {
                $checkStatus = "FAIL"
                $message = "$($script.Name) failed with exit code $LASTEXITCODE"
            }
            $exitCode = $LASTEXITCODE
        }
        catch {
            $checkStatus = "FAIL"
            $message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
            $exitCode = 1
        }
        finally {
            $global:LASTEXITCODE = 0
        }

        $checkEnd = (Get-Date).ToUniversalTime()
        $checks += [ordered]@{
            id = [System.IO.Path]::GetFileNameWithoutExtension($script.Name)
            script = ConvertTo-AINpcRepoRelativePath $script.FullName
            status = $checkStatus
            exitCode = $exitCode
            durationMs = [int][Math]::Max(0, [Math]::Round(($checkEnd - $checkStart).TotalMilliseconds))
            message = $message
        }

        if ($checkStatus -ne "PASS") {
            $status = "FAIL"
            $failures += [ordered]@{
                id = [System.IO.Path]::GetFileNameWithoutExtension($script.Name)
                message = $message
                artifact = ConvertTo-AINpcRepoRelativePath $script.FullName
            }
        }
    }
}

$endTime = (Get-Date).ToUniversalTime()
Write-AINpcTestResult -Context $context -Status $status -StartTimeUtc $startTime -EndTimeUtc $endTime -Command $command -Artifacts ([ordered]@{
    result = ConvertTo-AINpcRepoRelativePath $context.ResultPath
}) -Diagnostics ([ordered]@{
    discoveredCount = $verifyScripts.Count
    failedCount = @($checks | Where-Object { $_.status -eq "FAIL" }).Count
    blockedCount = @($checks | Where-Object { $_.status -eq "BLOCKED" }).Count
}) -Observations ([ordered]@{
    checks = @($checks)
}) -Failures $failures | Out-Null

if ($status -eq "PASS") {
    Write-Host "PASS: all static verification scripts completed"
    exit 0
}

Write-Host "FAIL: static verification completed with status $status"
exit 1
