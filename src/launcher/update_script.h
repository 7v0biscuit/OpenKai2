// SPDX-License-Identifier: GPL-3.0-only
#pragma once
namespace openkai2::launcher {
// Runs in the Windows inbox PowerShell 5.1 host. No downloaded script is executed.
inline constexpr char update_script[] = R"OPENKAI2(
param(
    [Parameter(Mandatory=$true)][int]$ParentId,
    [int]$WrapperId = 0,
    [Parameter(Mandatory=$true)][string]$ReadyEvent,
    [Parameter(Mandatory=$true)][string]$Package,
    [Parameter(Mandatory=$true)][string]$Expected,
    [Parameter(Mandatory=$true)][string]$Target,
    [Parameter(Mandatory=$true)][string]$Version,
    [int]$Portable = 0,
    [switch]$NoRestart
)
$ErrorActionPreference = 'Stop'
$stage = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Package))
$log = Join-Path $stage 'apply.log'
$rollback = $null
$changed = [Collections.Generic.List[object]]::new()
$locks = [Collections.Generic.List[Threading.Mutex]]::new()
try {
    # A PowerShell 7 parent can supply a PSModulePath that hides the Windows
    # PowerShell script functions. Load the inbox modules from this host's path.
    foreach ($module in @('Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Management')) {
        $manifestPath = [IO.Path]::Combine($PSHOME, 'Modules', $module, ($module + '.psd1'))
        Import-Module -Name $manifestPath -Force -ErrorAction Stop
    }
    $parentProcess = Get-Process -Id $ParentId -ErrorAction SilentlyContinue
    $wrapperProcess = if ($WrapperId -gt 0) { Get-Process -Id $WrapperId -ErrorAction SilentlyContinue }
    $ready = [Threading.EventWaitHandle]::OpenExisting($ReadyEvent)
    $ready.Set() | Out-Null
    $ready.Dispose()
    if ($parentProcess) { $parentProcess.WaitForExit() }
    if ($wrapperProcess) { $wrapperProcess.WaitForExit() }
    foreach ($name in @('Local\OpenKai2.Launcher', 'Local\OpenKai2.GameAndSaveSession')) {
        $mutex = [Threading.Mutex]::new($false, $name)
        try { $held = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held = $true }
        if (!$held) { $mutex.Dispose(); throw 'Another launcher, game or save operation is running. Close it and try updating again.' }
        $locks.Add($mutex)
    }
    if ($Expected -notmatch '^[0-9a-f]{64}$' -or (Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash.ToLowerInvariant() -cne $Expected) {
        throw 'The downloaded update failed its SHA-256 check. The existing application was kept.'
    }
    $targetPath = [IO.Path]::GetFullPath($Target)
    $targetDirectory = if ($Portable) { [IO.Path]::GetDirectoryName($targetPath) } else { $targetPath }
    for ($p = [IO.DirectoryInfo]::new($targetDirectory); $p; $p = $p.Parent) {
        if (($p.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Update destination contains a link. Update this copy manually.' }
    }
    $payload = Join-Path $stage 'payload'
    [IO.Directory]::CreateDirectory($payload) | Out-Null
    $files = [Collections.Generic.List[object]]::new()
    if ($Portable) {
        if ([IO.Path]::GetExtension($targetPath) -ine '.exe') { throw 'Invalid portable update destination.' }
        $files.Add([pscustomobject]@{ Source=$Package; Destination=$targetPath; Hash=$Expected })
    } else {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($Package)
        try {
            $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            [long]$total = 0
            if ($archive.Entries.Count -gt 4096) { throw 'Too many update files.' }
            foreach ($entry in $archive.Entries) {
                $name = $entry.FullName
                if (!$name.StartsWith('OpenKai2/', [StringComparison]::Ordinal) -or $name -notmatch '^[A-Za-z0-9_.+/-]+$') { throw 'Unsafe update archive path.' }
                $relative = $name.Substring(9).TrimEnd('/')
                if (!$relative) { continue }
                foreach ($part in $relative.Split('/')) {
                    if (!$part -or $part -eq '.' -or $part -eq '..' -or $part.EndsWith('.') -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'Unsafe update archive path.' }
                }
                if (!$seen.Add($relative)) { throw 'Duplicate update archive path.' }
                if (($entry.ExternalAttributes -shr 16 -band 0xf000) -eq 0xa000) { throw 'Linked update archive file.' }
                $total += $entry.Length
                if ($entry.Length -gt 512MB -or $total -gt 1GB) { throw 'Update archive is too large.' }
                if ($name.EndsWith('/')) { continue }
                $out = Join-Path $payload $relative
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($out)) | Out-Null
                $stream = [IO.File]::Open($out, [IO.FileMode]::CreateNew)
                $entryStream = $entry.Open()
                try { $entryStream.CopyTo($stream) } finally { $entryStream.Dispose(); $stream.Dispose() }
            }
        } finally { $archive.Dispose() }
        $root = $payload
        $manifest = Get-Content -LiteralPath (Join-Path $root 'manifest.json') -Raw | ConvertFrom-Json
        if ($manifest.architecture -cne 'windows-x64' -or $manifest.version -cne $Version -or !$manifest.files_sha256) { throw 'The update manifest does not match this release.' }
        $listed = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($property in $manifest.files_sha256.PSObject.Properties) {
            $relative = $property.Name
            if (!$seen.Contains($relative) -or !$listed.Add($relative) -or $relative -ieq 'manifest.json' -or $property.Value -notmatch '^[0-9a-f]{64}$') { throw 'Invalid update manifest entry.' }
            $source = Join-Path $root $relative
            if (!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -cne $property.Value) { throw 'An update payload hash does not match.' }
            $files.Add([pscustomobject]@{ Source=$source; Destination=(Join-Path $targetDirectory $relative); Hash=$property.Value })
        }
        $actualFiles = @(Get-ChildItem -LiteralPath $root -File -Recurse)
        if ($actualFiles.Count -ne $listed.Count + 1 -or !$listed.Contains('OpenKai2.exe')) { throw 'The update manifest does not cover every payload file.' }
        $files.Add([pscustomobject]@{ Source=(Join-Path $root 'manifest.json'); Destination=(Join-Path $targetDirectory 'manifest.json'); Hash=(Get-FileHash -LiteralPath (Join-Path $root 'manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() })
    }
    $rollback = Join-Path $targetDirectory ('.openkai2-update-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($rollback) | Out-Null
    $index = 0
    foreach ($file in $files) {
        $destination = $file.Destination
        for ($p = [IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($destination)); $p; $p = $p.Parent) {
            if ($p.Exists -and ($p.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Update destination contains a link.' }
        }
        if (Test-Path -LiteralPath $destination) {
            $existing = Get-Item -LiteralPath $destination -Force
            if ($existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Update destination is not a regular file.' }
        }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        $backup = Join-Path $rollback ($index.ToString() + '.previous')
        $temporary = Join-Path $rollback ($index.ToString() + '.new')
        [IO.File]::Copy($file.Source, $temporary, $false)
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.Hash) { throw 'Staged file verification failed.' }
        $hadOriginal = Test-Path -LiteralPath $destination
        if ($hadOriginal) { [IO.File]::Move($destination, $backup) }
        $changed.Add([pscustomobject]@{ Destination=$destination; Backup=$backup; HadOriginal=$hadOriginal })
        [IO.File]::Move($temporary, $destination)
        $index++
    }
    # The installer registration belongs only to installed copies at this exact path.
    if (!$Portable) {
        $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenKai2'
        $registration = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($registration -and $registration.InstallLocation -ieq $targetDirectory) {
            Set-ItemProperty -LiteralPath $key -Name DisplayVersion -Value $Version
        }
    }
    'Updated to ' + $Version | Set-Content -LiteralPath $log -Encoding UTF8
    $changed.Clear()
    Remove-Item -LiteralPath $rollback -Recurse -Force -ErrorAction SilentlyContinue
    $rollback = $null
    Remove-Item -LiteralPath $Package -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $payload -Recurse -Force -ErrorAction SilentlyContinue
    foreach ($mutex in $locks) { $mutex.ReleaseMutex(); $mutex.Dispose() }
    $locks.Clear()
    if (!$NoRestart) {
        $executable = if ($Portable) { $targetPath } else { Join-Path $targetDirectory 'OpenKai2.exe' }
        Start-Process -FilePath $executable -WorkingDirectory $targetDirectory
    }
    exit 0
} catch {
    $failure = $_.Exception.Message
    for ($i = $changed.Count - 1; $i -ge 0; $i--) {
        $file = $changed[$i]
        try {
            if (Test-Path -LiteralPath $file.Destination) { Remove-Item -LiteralPath $file.Destination -Force }
            if ($file.HadOriginal) { [IO.File]::Move($file.Backup, $file.Destination) }
        } catch { $failure += "`r`nRecovery copy: " + $file.Backup }
    }
    $failure | Set-Content -LiteralPath $log -Encoding UTF8
    if (!$NoRestart) {
        Add-Type -AssemblyName PresentationFramework
        [System.Windows.MessageBox]::Show($failure + "`r`n`r`nDetails: " + $log, 'OpenKai2 update') | Out-Null
    }
    exit 1
} finally {
    foreach ($mutex in $locks) { try { $mutex.ReleaseMutex() } catch {}; $mutex.Dispose() }
}
)OPENKAI2";
}
