#Requires -Version 5.1
#Requires -RunAsAdministrator
#Requires -Modules NetSecurity

<#
.SYNOPSIS
Manually configure outbound block rules for a local GUI/client pair.
.DESCRIPTION
Does not start a program, download anything, elevate itself, disable Defender,
or change global firewall settings. Only Proxmark3GUI.exe and proxmark3.exe are
accepted; unknown helpers, scripts, network paths and reparse points are refused.
Filename and PE-header checks do not authenticate either executable.

Rules apply only to the two specified paths, not to child processes or services.
The local Windows Firewall policy must be enabled and effective. Domain policy
can prevent local rules from taking effect. See OFFLINE.md before manual use.
.PARAMETER Remove
Remove only this script's two path-specific rules. The files may already have
been removed, but their original absolute paths must be supplied.
.EXAMPLE
.\Set-OfflineFirewall.ps1 -GuiPath 'C:\PM3GUI\Proxmark3GUI.exe' -ClientPath 'C:\PM3Client\proxmark3.exe' -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $GuiPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $ClientPath,

    [switch] $Remove
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ruleGroup = 'Proxmark3GUIOffline.LocalOnly'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This manual script requires Windows.'
}

function Resolve-LocalExecutable {
    param([string] $Path, [string] $ExpectedName, [bool] $MustExist)

    if ($Path -ne $Path.Trim() -or $Path -match '[\x00-\x1f]') {
        throw 'Use an absolute local path without surrounding whitespace or control characters.'
    }
    if ($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Substring(2).Contains(':')) {
        throw 'Only absolute local drive paths are accepted; URLs, UNC paths and device paths are refused.'
    }
    $fullPath = [IO.Path]::GetFullPath($Path)
    if ([IO.Path]::GetFileName($fullPath) -ine $ExpectedName) {
        throw "Expected $ExpectedName. Unknown helper programs and launchers are not accepted."
    }
    $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($fullPath))
    if ($drive.DriveType -notin @([IO.DriveType]::Fixed, [IO.DriveType]::Removable)) {
        throw 'Network, unresolved and non-local drives are not accepted.'
    }

    # Walk from the drive root downward. Checking the full path first could
    # traverse an unreviewed junction before detecting it in a parent folder.
    $component = [IO.Path]::GetPathRoot($fullPath)
    $parts = $fullPath.Substring($component.Length) -split '[\\/]'
    foreach ($part in $parts) {
        if ($part.EndsWith('.') -or $part.EndsWith(' ')) {
            throw 'Path components ending in a dot or space are not accepted.'
        }
        $component = [IO.Path]::Combine($component, $part)
        if (Test-Path -LiteralPath $component) {
            $item = Get-Item -LiteralPath $component -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Symbolic links, junctions and other reparse points are not accepted.'
            }
        }
    }

    if ($MustExist) {
        $file = Get-Item -LiteralPath $fullPath -Force
        if ($file -isnot [IO.FileInfo]) { throw 'Select an existing regular executable file.' }
        $stream = [IO.File]::Open($fullPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $reader = New-Object IO.BinaryReader $stream
        try {
            if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) {
                throw 'The selected file does not have a native Windows executable header.'
            }
            $stream.Position = 0x3c
            $peOffset = $reader.ReadInt32()
            if ($peOffset -lt 64 -or $peOffset -gt ($stream.Length - 24)) {
                throw 'The selected file has an invalid PE header offset.'
            }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) { throw 'The selected file has no PE signature.' }
            $stream.Position = $peOffset + 22
            if (($reader.ReadUInt16() -band 0x2000) -ne 0) { throw 'A DLL is not a client executable.' }
        } finally {
            $reader.Dispose()
            $stream.Dispose()
        }
    }
    return $fullPath
}

function Get-RuleName {
    param([string] $Role, [string] $Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Path.ToLowerInvariant())
        $suffix = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').Substring(0, 24)
        return "Proxmark3GUIOffline.Outbound.$Role.$suffix"
    } finally {
        $sha.Dispose()
    }
}

$gui = Resolve-LocalExecutable $GuiPath 'Proxmark3GUI.exe' (-not $Remove)
$client = Resolve-LocalExecutable $ClientPath 'proxmark3.exe' (-not $Remove)
$targets = @(
    @{ Role = 'GUI'; Path = $gui },
    @{ Role = 'Client'; Path = $client }
)

# Validate both inputs and all existing rule ownership before making changes.
foreach ($target in $targets) {
    $target.Name = Get-RuleName $target.Role $target.Path
    $existing = @(Get-NetFirewallRule -PolicyStore PersistentStore -Name $target.Name -ErrorAction SilentlyContinue)
    foreach ($rule in $existing) {
        $application = $rule | Get-NetFirewallApplicationFilter
        if ($rule.Group -cne $ruleGroup -or $application.Program -ine $target.Path) {
            throw "A conflicting rule exists: $($target.Name). No rule has been changed."
        }
    }
    $target.Existing = $existing
}

foreach ($target in $targets) {
    if ($Remove) {
        if ($target.Existing.Count -gt 0 -and $PSCmdlet.ShouldProcess($target.Path, 'Remove this GUI/client outbound block rule')) {
            $target.Existing | Remove-NetFirewallRule
        }
    } elseif ($PSCmdlet.ShouldProcess($target.Path, 'Configure outbound block rule for all profiles')) {
        # Recreate only our exact path-specific rule so edited port/address
        # filters cannot silently narrow the block on a subsequent manual run.
        if ($target.Existing.Count -gt 0) {
            $target.Existing | Remove-NetFirewallRule
        }
        New-NetFirewallRule -PolicyStore PersistentStore -Name $target.Name `
            -DisplayName "Proxmark3GUI Offline - block outbound - $($target.Role)" `
            -Group $ruleGroup -Direction Outbound -Action Block -Enabled True `
            -Profile Any -Program $target.Path | Out-Null
    }
}

Write-Output "Rule group: $ruleGroup"
Write-Output 'These rules cover only the specified executable paths. They do not cover helpers, child processes or services.'
Write-Output 'Verify the effective Windows Firewall policy before relying on the rules; this script does not change global policy.'
