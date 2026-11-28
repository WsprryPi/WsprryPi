# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true, Position=0)]
    [ValidateSet('install','uninstall')][string]$Action,
    [string]$Bundle,
    [ValidatePattern('^[0-9a-f]{64}$')][string]$Sha256,
    [string]$Python,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$system = 'S-1-5-18'
$admin = 'S-1-5-32-544'
$programParent = [Environment]::GetFolderPath('ProgramFiles')
$dataParent = [Environment]::GetFolderPath('CommonApplicationData')
$program = Join-Path $programParent 'WsprryPi'
$releases = Join-Path $programParent 'WsprryPiControllerReleases'
$data = Join-Path $dataParent 'WsprryPi'
$configuration = Join-Path $data 'Configuration'
$receiptPath = Join-Path $data 'installation.json'
$bootstrapGate = Join-Path $configuration 'admit-controller'
$backendState = Join-Path $data 'Controller\application-state'
$listenerResult = Join-Path $data 'Controller\Run\installer-listeners.json'
$jobsPath = Join-Path $data 'Maintenance\jobs.json'
$logs = Join-Path $data 'Http'
$proxyDirectory = Join-Path $configuration 'Proxy'
$proxyPath = Join-Path $proxyDirectory 'proxy.conf'
$exe = Join-Path $program 'bin\wsprrypi.exe'
$http = Join-Path $program 'runtime\apache\bin\httpd.exe'
$httpConfig = Join-Path $configuration 'httpd.conf'
$phpConfig = Join-Path $configuration 'php.ini'
$encode = [Text.UTF8Encoding]::new($false)
$sc = Join-Path ([Environment]::SystemDirectory) 'sc.exe'
$names = [ordered]@{
    Maintenance = 'WsprryPiMaintenance'
    Controller = 'WsprryPiController'
    Web = 'WsprryPiWeb'
    Http = 'WsprryPiHttp'
}
$commands = @{
    Maintenance = '"' + $exe + '" --native-maintenance-service'
    Controller = '"' + $exe + '" --native-controller-service --controller-only'
    Web = '"' + $exe + '" --native-web-service'
    Http = '"' + $http + '" -k runservice'
}
$ids = @{}

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new($File)
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.CreateNoWindow = $true
    $start.Arguments = ($Arguments | ForEach-Object {
        '"' + ($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1') + '"'
    }) -join ' '
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(180000)) {
            $process.Kill()
            throw "Command timed out: $File $($Arguments[0])"
        }
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "Command failed ($($process.ExitCode)): $File $($Arguments[0]) $($errors.Trim())"
        }
        return $output
    } finally { $process.Dispose() }
}

function Invoke-InstallerPython([string]$Script, [string[]]$Arguments) {
    # Embedded CPython omits the script directory. Import only the installer’s
    # companion modules, without using the calling account’s Python environment.
    $bootstrap = 'import os,runpy,sys; del sys.argv[0]; sys.path.insert(0,os.path.dirname(sys.argv[0])); runpy.run_path(sys.argv[0],run_name="__main__")'
    return Invoke-Checked $Python (@('-I','-B','-c',$bootstrap,(Join-Path $PSScriptRoot $Script)) + $Arguments)
}

function Existing([string]$Path) {
    try { return (Get-Item -LiteralPath $Path -Force -ErrorAction Stop) }
    catch [Management.Automation.ItemNotFoundException] { return $null }
}

function Assert-Shape([string]$Path, [bool]$Directory) {
    $item = Existing $Path
    if ($null -eq $item -or $item.PSIsContainer -ne $Directory -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Untrusted controller path: $Path"
    }
}

function New-Policy([bool]$Directory, [string]$Owner, [string[]]$Readers, [bool]$InheritReaders) {
    $acl = if ($Directory) { [Security.AccessControl.DirectorySecurity]::new() } else { [Security.AccessControl.FileSecurity]::new() }
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new($Owner))
    $acl.SetAccessRuleProtection($true, $false)
    $descendants = if ($Directory) { 'ContainerInherit,ObjectInherit' } else { 'None' }
    foreach ($sid in @($Owner, $system, $admin) | Select-Object -Unique) {
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($sid), 'FullControl', $descendants, 'None', 'Allow'))
    }
    foreach ($sid in $Readers | Sort-Object) {
        $inheritance = if ($InheritReaders) { $descendants } else { 'None' }
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($sid), 'ReadAndExecute', $inheritance, 'None', 'Allow'))
    }
    return $acl
}

function Access-Rules($Policy, [bool]$Directory) {
    $Policy.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier]) | ForEach-Object {
        $scope = if ($Directory) { "$($_.InheritanceFlags)/$($_.PropagationFlags)" } else { 'file' }
        "$($_.IdentityReference.Value)/$($_.AccessControlType)/$($_.FileSystemRights)/$scope/$($_.IsInherited)"
    }
}

function Policy-Matches($Actual, $Required, [bool]$Directory) {
    $sidType = [Security.Principal.SecurityIdentifier]
    return ($Actual.AreAccessRulesProtected -and $Actual.GetOwner($sidType).Value -ceq $Required.GetOwner($sidType).Value -and
        -not (Compare-Object @(Access-Rules $Actual $Directory) @(Access-Rules $Required $Directory)))
}

function Assert-Policy([string]$Path, [bool]$Directory, $Policy) {
    Assert-Shape $Path $Directory
    if (-not (Policy-Matches (Get-Acl -LiteralPath $Path) $Policy $Directory)) {
        throw "Controller owner or ACL differs: $Path"
    }
}

function Create-Directory([string]$Path, $Policy) {
    if ($null -eq (Existing $Path)) { [void][IO.Directory]::CreateDirectory($Path, $Policy) }
    Assert-Policy $Path $true $Policy
}

function Digest([string]$Path) {
    Assert-Shape $Path $false
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-Json([string]$Path, [int]$Limit) {
    Assert-Shape $Path $false
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.Length -le 0 -or $file.Length -gt $Limit) { throw "Invalid record size: $Path" }
    return ([IO.File]::ReadAllText($Path, $encode) | ConvertFrom-Json)
}

function Assert-Fields($Value, [string[]]$Fields) {
    if ($null -eq $Value -or $Value -isnot [pscustomobject] -or
        (Compare-Object @($Value.PSObject.Properties.Name | Sort-Object) @($Fields | Sort-Object))) {
        throw 'Unexpected controller record fields'
    }
}

function Write-Owned([string]$Path, [string]$Content, $Policy) {
    if ($null -ne (Existing $Path)) { Assert-Policy $Path $false $Policy }
    $temporary = Join-Path (Split-Path -Parent $Path) ('.wsprrypi-' + [Guid]::NewGuid().ToString('n'))
    try {
        $stream = [IO.FileStream]::new($temporary, 'CreateNew', 'Write', 'None')
        try {
            $bytes = $encode.GetBytes($Content)
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush($true)
        } finally { $stream.Dispose() }
        # .NET clears the modification flags on an ACL after applying it.
        # Each publication must apply a fresh descriptor to its new file.
        $pendingPolicy = [Security.AccessControl.FileSecurity]::new()
        $pendingPolicy.SetSecurityDescriptorBinaryForm($Policy.GetSecurityDescriptorBinaryForm(),
            [Security.AccessControl.AccessControlSections]'Owner,Access')
        [IO.File]::SetAccessControl($temporary, $pendingPolicy)
        Assert-Policy $temporary $false $Policy
        [WsprryPiInstaller]::Publish($temporary, $Path, $null -ne (Existing $Path))
        Assert-Policy $Path $false $Policy
    } finally {
        if ($null -ne (Existing $temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Save-Receipt($Receipt) {
    Write-Owned $receiptPath (($Receipt | ConvertTo-Json -Depth 6 -Compress) + "`n") (New-Policy $false $system @() $false)
}

function Read-Receipt {
    Assert-Policy $receiptPath $false (New-Policy $false $system @() $false)
    $value = Read-Json $receiptPath 1048576
    Assert-Fields $value @('schema','active','slots')
    if ($value.schema -cne 'windows-controller-installation/1' -or $value.slots -isnot [array]) {
        throw 'Unknown controller installation receipt'
    }
    $slots = @{}
    foreach ($slot in $value.slots) {
        Assert-Fields $slot @('archive','manifest')
        if ($slot.archive -cnotmatch '^[0-9a-f]{64}$' -or $slot.manifest -cnotmatch '^[0-9a-f]{64}$' -or
            $slots.ContainsKey($slot.archive) -or $slots.ContainsValue($slot.manifest)) {
            throw 'Invalid or ambiguous controller release slot'
        }
        $slots[$slot.archive] = $slot.manifest
    }
    if ($null -ne $value.active -and -not $slots.ContainsKey($value.active)) { throw 'Unknown active controller release' }
    return $value
}

function Service-Record([string]$Role) {
    $name = $names[$Role]
    return Get-CimInstance Win32_Service -Filter "Name='$name'"
}

function Assert-Service([string]$Role, $Service) {
    $name = $names[$Role]
    if ($Service.StartName -ine "NT SERVICE\$name" -or $Service.PathName -cne $commands[$Role] -or
        $Service.ServiceType -ne 'Own Process') {
        throw "Unowned or mismatched controller service: $name"
    }
}

function Wait-Service([string]$Role, [string]$State) {
    $limit = [DateTime]::UtcNow.AddSeconds(100)
    do {
        $service = Service-Record $Role
        if ($null -eq $service) { throw "Controller service disappeared: $Role" }
        Assert-Service $Role $service
        if ($service.State -eq $State) { return $service }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $limit)
    throw "Controller service did not reach ${State}: $Role"
}

function Require-Idle {
    $maintenance = Service-Record Maintenance
    if ($maintenance -and $maintenance.State -eq 'Running') {
        $record = (Invoke-Checked $exe @('--maintenance','jobs')) | ConvertFrom-Json
        if ($record.schema -cne 'recovery-job-list/1' -or $null -ne $record.active) {
            throw 'Recovery work is active'
        }
    } elseif ($null -ne (Existing $jobsPath)) {
        Assert-Policy $jobsPath $false (New-Policy $false $ids.Maintenance @() $false)
        $record = Read-Json $jobsPath 1048576
        Assert-Fields $record @('schema','identity','role','active','jobs')
        if ($record.schema -cne 'recovery-jobs/1' -or $record.role -cne 'controller' -or $null -ne $record.active -or
            $record.jobs -isnot [array] -or $record.jobs.Count -gt 4 -or
            @($record.jobs | Where-Object { $_ -cnotmatch '^[0-9a-f]{32}$' }).Count -or
            @($record.jobs | Select-Object -Unique).Count -ne $record.jobs.Count) {
            throw 'A stopped maintenance worker retains unresolved recovery work'
        }
    }
}

function Stop-Role([string]$Role) {
    $service = Service-Record $Role
    if ($service -and $service.State -ne 'Stopped') {
        Stop-Service -Name $names[$Role] -ErrorAction Stop
        [void](Wait-Service $Role 'Stopped')
    }
}

function Quiesce {
    Require-Idle
    $running = @('Http','Web') | Where-Object {
        $service = Service-Record $_
        $service -and $service.State -eq 'Running'
    }
    Stop-Role Http
    Stop-Role Web
    try { Require-Idle }
    catch {
        foreach ($role in @('Web','Http')) { if ($role -in $running) { Start-Service -Name $names[$role] } }
        throw
    }
    foreach ($role in $names.Keys) {
        if ($null -ne (Service-Record $role)) {
            Set-Service -Name $names[$role] -StartupType Manual
        }
    }
    Stop-Role Controller
    Stop-Role Maintenance
}

function Install-SlotAcl([string]$Root) {
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue($Root)
    while ($pending.Count) {
        $path = $pending.Dequeue()
        Assert-Shape $path $true
        [IO.Directory]::SetAccessControl($path, (New-Policy $true $system @($ids.Values) $true))
        foreach ($item in Get-ChildItem -LiteralPath $path -Force) {
            Assert-Shape $item.FullName $item.PSIsContainer
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
            else { [IO.File]::SetAccessControl($item.FullName, (New-Policy $false $system @($ids.Values) $false)) }
        }
    }
}

function Assert-Slot([string]$Root, [string]$ManifestHash) {
    $candidate = Join-Path $Root 'controller-bundle.json'
    if ((Digest $candidate) -cne $ManifestHash) { throw 'Controller release manifest changed' }
    $manifest = Read-Json $candidate 4194304
    $expected = @{'controller-bundle.json' = $ManifestHash}
    foreach ($item in $manifest.files.PSObject.Properties) { $expected[$item.Name] = $item.Value }
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue($Root)
    while ($pending.Count) {
        $path = $pending.Dequeue()
        Assert-Policy $path $true (New-Policy $true $system @($ids.Values) $true)
        foreach ($item in Get-ChildItem -LiteralPath $path -Force) {
            Assert-Shape $item.FullName $item.PSIsContainer
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName); continue }
            Assert-Policy $item.FullName $false (New-Policy $false $system @($ids.Values) $false)
            $name = $item.FullName.Substring($Root.Length + 1).Replace('\','/')
            if (-not $expected.ContainsKey($name) -or (Digest $item.FullName) -cne $expected[$name]) {
                throw "Controller release content changed: $name"
            }
            $expected.Remove($name)
        }
    }
    if ($expected.Count) { throw 'Controller release is incomplete' }
    return $manifest
}

function Archive-Active($Receipt) {
    if ($null -eq (Existing $program)) { return }
    Assert-Shape $program $true
    $manifestHash = Digest (Join-Path $program 'controller-bundle.json')
    $found = @($Receipt.slots | Where-Object { $_.manifest -ceq $manifestHash })
    if ($found.Count -ne 1) { throw 'Unowned active controller release' }
    [void](Assert-Slot $program $manifestHash)
    $target = Join-Path $releases $found[0].archive
    if ($null -ne (Existing $target)) { throw 'Duplicate active controller release' }
    [IO.Directory]::Move($program, $target)
}

function Write-Hosting([int]$Web, [int]$Socket) {
    $result = (Invoke-InstallerPython 'controller_apache.py' @(
        '--program',$program,'--configuration',$configuration,'--logs',$logs,
        '--proxy',$proxyPath,'--web',"$Web",'--socket',"$Socket")) | ConvertFrom-Json
    Assert-Fields $result @('http','proxy','php')
    Write-Owned $httpConfig $result.http (New-Policy $false $system @($ids.Http,$ids.Maintenance) $false)
    Write-Owned $phpConfig $result.php (New-Policy $false $system @($ids.Http,$ids.Maintenance) $false)
    Write-Owned $proxyPath $result.proxy (New-Policy $false $ids.Maintenance @($ids.Http) $false)
    [void](Invoke-Checked $http @('-t','-f',$httpConfig))
    # Use Apache's SCM parameter format without its -k config command, which
    # would enable the service before the installer has verified readiness.
    $key = [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey(
        "SYSTEM\CurrentControlSet\Services\$($names.Http)\Parameters")
    try {
        $expected = [string[]]@('-f',$httpConfig,'-E',(Join-Path $logs 'startup.log'))
        $values = $key.GetValueNames()
        if ($values.Count -eq 0) {
            $key.SetValue('ConfigArgs',$expected,[Microsoft.Win32.RegistryValueKind]::MultiString)
        } elseif ($values.Count -ne 1 -or $values[0] -cne 'ConfigArgs' -or
            $key.GetValueKind('ConfigArgs') -ne [Microsoft.Win32.RegistryValueKind]::MultiString -or
            (ConvertTo-Json -InputObject ($key.GetValue('ConfigArgs')) -Compress) -cne (ConvertTo-Json -InputObject $expected -Compress)) {
            throw 'Unexpected Apache service parameters'
        }
    } finally { $key.Dispose() }
}

function Provision-State([string]$Operation) {
    $gatePolicy = New-Policy $false $system @($ids.Controller) $false
    Write-Owned $bootstrapGate "wsprrypi-controller-bootstrap/1 $Operation`n" $gatePolicy
    try {
        [void](Invoke-Checked $sc @('start',$names.Controller,"--$Operation-controller-state"))
        $final = Wait-Service Controller 'Stopped'
        if ($final.ExitCode -ne 0) { throw "Controller state $Operation failed" }
    } finally {
        Assert-Policy $bootstrapGate $false $gatePolicy
        [IO.File]::Delete($bootstrapGate)
    }
}

function Wait-Ready($Manifest) {
    Add-Type -AssemblyName System.Net.Http
    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.UseProxy = $false
    $handler.AllowAutoRedirect = $false
    $client = [Net.Http.HttpClient]::new($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(3)
    try {
        $limit = [DateTime]::UtcNow.AddSeconds(70)
        do {
            $version = $null
            try {
                $reply = $client.GetStringAsync('http://127.0.0.1:8080/wsprrypi/version').GetAwaiter().GetResult()
                if ($reply.Length -lt 131072) { $version = $reply | ConvertFrom-Json }
            } catch [Net.Http.HttpRequestException] {}
              catch [Threading.Tasks.TaskCanceledException] {}
            if ($version -and $version.host_status -and $version.host_status.controller_ready -eq $true) {
                if ($version.host_status.role -cne 'controller-only' -or $version.ancillary_gpio -ne $false -or
                    $version.compiled_backends -cne '' -or $version.wspr_commit -cne $Manifest.binary.source_commit -or
                    $version.wspr_exe_version -cne $Manifest.binary.application_version) {
                    throw 'Unexpected installed controller identity'
                }
                foreach ($role in $names.Keys) {
                    $service = Wait-Service $role 'Running'
                    $owner = Invoke-CimMethod -InputObject (Get-CimInstance Win32_Process -Filter "ProcessId=$($service.ProcessId)") -MethodName GetOwnerSid
                    if ($owner.ReturnValue -ne 0 -or $owner.Sid -cne $ids[$role]) {
                        throw "Unexpected effective service identity: $role"
                    }
                }
                return
            }
            Start-Sleep -Milliseconds 300
        } while ([DateTime]::UtcNow -lt $limit)
        throw 'Installed controller did not become ready through Apache'
    } finally { $client.Dispose() }
}

function Wait-ControllerEndpoint([string]$Identity) {
    $limit = [DateTime]::UtcNow.AddSeconds(70)
    do {
        $observed = $null
        try { $observed = (Invoke-Checked $exe @('--recovery-backend','observe')) | ConvertFrom-Json }
        catch {}
        if ($observed -and $observed.ready -eq $true) {
            if ($observed.identity -cne $Identity) { throw 'Unexpected installed controller identity' }
            return
        }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $limit)
    throw 'Controller backend did not become ready through its private local control endpoint'
}

if ($Action -eq 'install' -and (-not $Bundle -or -not $Sha256)) { throw 'Supply a local bundle and its SHA-256' }
if ($Action -eq 'uninstall' -and ($Bundle -or $Sha256 -or $DryRun -or $Python)) { throw 'Uninstall accepts no artifact options' }
if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion -lt [version]'5.1') {
    throw 'Use 64-bit Windows PowerShell 5.1'
}
if ([Environment]::OSVersion.Version.Build -lt 26300 -or
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64' -or -not [Environment]::Is64BitProcess) {
    throw 'This installer requires Windows x64 build 26300 or later and 64-bit PowerShell'
}
if ($Action -eq 'install') {
    if (-not $Python) { $Python = (Get-Command python.exe -CommandType Application).Source }
    if (-not [IO.Path]::IsPathRooted($Python)) { throw 'Supply an absolute Python executable path' }
    Assert-Shape $Python $false
    $Bundle = (Resolve-Path -LiteralPath $Bundle).ProviderPath
    $verifyArguments = @('verify','--platform','windows-26300','--architecture','amd64','--sha256',$Sha256)
    if ($DryRun) { Invoke-InstallerPython 'controller_bundle.py' ($verifyArguments + @($Bundle)); return }
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Windows controller installation requires an elevated 64-bit PowerShell'
}
if ($Action -eq 'install') { [void](Invoke-InstallerPython 'controller_bundle.py' ($verifyArguments + @($Bundle))) }
if ($Action -eq 'install') {
    foreach ($library in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        $path = Join-Path ([Environment]::SystemDirectory) $library
        Assert-Shape $path $false
        $native = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
        if ([version]::new($native.FileMajorPart,$native.FileMinorPart,$native.FileBuildPart,$native.FilePrivatePart) -lt [version]'14.52.37029.0') {
            throw 'Install the x64 Microsoft Visual C++ Redistributable 14.52.37029 or later before installing the controller'
        }
    }
}

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class WsprryPiInstaller {
    [StructLayout(LayoutKind.Sequential)] struct Luid { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)] struct Privilege { public uint Count; public Luid Id; public uint Attributes; }
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool MoveFileExW(string from, string to, uint flags);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool LookupPrivilegeValueW(string system, string name, out Luid id);
    [DllImport("advapi32.dll", SetLastError=true)]
    static extern bool AdjustTokenPrivileges(IntPtr token, bool disable, ref Privilege state, uint size, IntPtr old, IntPtr needed);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr OpenSCManagerW(string machine, string database, uint access);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr CreateServiceW(IntPtr manager, string name, string display, uint access, uint type,
        uint start, uint error, string binary, string group, IntPtr tag, string dependencies, string account, string password);
    [DllImport("advapi32.dll")] static extern bool CloseServiceHandle(IntPtr handle);

    public static void EnableOwnership() {
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), 0x28, out token)) throw new Win32Exception();
        try {
            Luid id;
            if (!LookupPrivilegeValueW(null, "SeRestorePrivilege", out id)) throw new Win32Exception();
            var privilege = new Privilege { Count=1, Id=id, Attributes=2 };
            if (!AdjustTokenPrivileges(token, false, ref privilege, 0, IntPtr.Zero, IntPtr.Zero)
                || Marshal.GetLastWin32Error()!=0) throw new Win32Exception();
        } finally { CloseHandle(token); }
    }

    public static void Create(string name, string binary) {
        var manager = OpenSCManagerW(null, null, 3);
        if (manager == IntPtr.Zero) throw new Win32Exception();
        try {
            var service = CreateServiceW(manager, name, name, 0xF01FF, 0x10, 3, 1, binary,
                null, IntPtr.Zero, null, "NT SERVICE\\" + name, null);
            if (service == IntPtr.Zero) throw new Win32Exception();
            CloseServiceHandle(service);
        } finally { CloseServiceHandle(manager); }
    }

    public static void Publish(string source, string target, bool replace) {
        for (int attempt = 0; ; ++attempt) {
            if (MoveFileExW(source, target, replace ? 9u : 8u)) return;
            int error = Marshal.GetLastWin32Error();
            // NTFS can report access denied for an existing file that is
            // open without delete sharing, even when both ACLs were admitted.
            bool sharingRefusal = error == 32 || error == 33 || (replace && error == 5);
            if (attempt >= 29 || !sharingRefusal) {
                throw new Win32Exception(error, target + ": " + new Win32Exception(error).Message);
            }
            System.Threading.Thread.Sleep(100);
        }
    }
}
'@
[WsprryPiInstaller]::EnableOwnership()

foreach ($parent in @($programParent,$dataParent)) {
    Assert-Shape $parent $true
    Assert-Shape ([IO.Path]::GetPathRoot($parent)) $true
    if ([IO.DriveInfo]::new([IO.Path]::GetPathRoot($parent)).DriveFormat -ne 'NTFS') {
        throw 'Controller installation requires NTFS'
    }
}
$privateDirectory = New-Policy $true $system @() $false
$privateFile = New-Policy $false $system @() $false
if ($null -eq (Existing $data)) {
    foreach ($path in @($program,$releases)) {
        if ($null -ne (Existing $path)) { throw "Unowned controller path: $path" }
    }
    foreach ($role in $names.Keys) {
        if ($null -ne (Service-Record $role)) { throw "Unowned controller service: $role" }
    }
    if ($Action -eq 'uninstall') { throw 'No owned Windows controller installation' }
    $bootstrap = Join-Path $dataParent ('.wsprrypi-' + [Guid]::NewGuid().ToString('n'))
    Create-Directory $bootstrap $privateDirectory
    try {
        Write-Owned (Join-Path $bootstrap 'installation.json') '{"schema":"windows-controller-installation/1","active":null,"slots":[]}' $privateFile
        [IO.Directory]::Move($bootstrap, $data)
    } finally {
        if ($null -ne (Existing $bootstrap)) { [IO.Directory]::Delete($bootstrap, $true) }
    }
} else { Assert-Shape $data $true }
$receipt = Read-Receipt
$lockPath = Join-Path $data 'installer.lock'
if ($null -eq (Existing $lockPath)) { Write-Owned $lockPath "wsprrypi-controller-installer/1`n" $privateFile }
Assert-Policy $lockPath $false $privateFile
$lock = [IO.File]::Open($lockPath, 'Open', 'ReadWrite', 'None')
$stage = $null
try {
    $receipt = Read-Receipt
    foreach ($role in $names.Keys) {
        $existingService = Service-Record $role
        if ($existingService) { Assert-Service $role $existingService }
    }

    if ($Action -eq 'install') {
        $stage = Join-Path $programParent ('.wsprrypi-stage-' + [Guid]::NewGuid().ToString('n'))
        Create-Directory $stage $privateDirectory
        Invoke-InstallerPython 'controller_bundle.py' ($verifyArguments + @('--stage',$stage,$Bundle))
        foreach ($role in $names.Keys) {
            if ($null -eq (Service-Record $role)) {
                [WsprryPiInstaller]::Create($names[$role], $commands[$role])
            }
        }
    }
    foreach ($role in $names.Keys) {
        $sidText = Invoke-Checked $sc @('showsid',$names[$role])
        $matches = [regex]::Matches($sidText,'\bS-1-5-80(?:-[0-9]+){5}\b')
        if ($matches.Count -ne 1) { throw "Cannot identify service SID: $role" }
        $ids[$role] = $matches[0].Value
        if ($null -ne (Service-Record $role)) {
            $account = [Security.Principal.NTAccount]::new('NT SERVICE',$names[$role])
            if ($account.Translate([Security.Principal.SecurityIdentifier]).Value -cne $ids[$role]) {
                throw "Registered controller service SID differs: $role"
            }
        }
    }
    if (@($ids.Values | Select-Object -Unique).Count -ne 4 -or
        @($ids.Values | Where-Object { $_ -cnotmatch '^S-1-5-80(-[0-9]+){5}$' }).Count) {
        throw 'Unexpected controller service account identities'
    }
    $rootPolicy = New-Policy $true $system @($ids.Values) $false
    if (Policy-Matches (Get-Acl -LiteralPath $data) $privateDirectory $true) {
        [IO.Directory]::SetAccessControl($data, $rootPolicy)
    }
    Assert-Policy $data $true $rootPolicy
    Create-Directory $releases $privateDirectory
    Create-Directory $configuration (New-Policy $true $system @($ids.Controller,$ids.Http,$ids.Maintenance) $false)

    foreach ($role in $names.Keys) {
        $roleRoot = Join-Path $data $role
        $policy = New-Policy $true $ids[$role] @() $false
        Create-Directory $roleRoot $policy
        Create-Directory (Join-Path $roleRoot 'Run') $policy
    }
    # Apache can traverse the public hosting configuration to this managed
    # include without any access to the private maintenance directory.
    Create-Directory $proxyDirectory (New-Policy $true $ids.Maintenance @($ids.Http) $false)
    $priorProgram = $null
    if ($null -ne (Existing $program)) {
        $priorManifest = Digest (Join-Path $program 'controller-bundle.json')
        $prior = @($receipt.slots | Where-Object { $_.manifest -ceq $priorManifest })
        if ($prior.Count -ne 1) { throw 'Unowned active controller program' }
        [void](Assert-Slot $program $priorManifest)
        $priorProgram = $prior[0].archive
    }
    foreach ($entry in @(@($httpConfig,$system,@($ids.Http,$ids.Maintenance)),@($phpConfig,$system,@($ids.Http,$ids.Maintenance)),
                          @($proxyPath,$ids.Maintenance,@($ids.Http)),@($bootstrapGate,$system,@($ids.Controller)))) {
        if ($null -ne (Existing $entry[0])) { Assert-Policy $entry[0] $false (New-Policy $false $entry[1] $entry[2] $false) }
    }
    if ($null -ne (Existing $backendState)) {
        Assert-Policy $backendState $true (New-Policy $true $ids.Controller @() $false)
    } elseif ($null -ne $receipt.active) {
        throw 'Established controller state is missing'
    }

    if ($Action -eq 'uninstall') {
        Quiesce
        Archive-Active $receipt
        foreach ($role in $names.Keys) {
            if ($null -ne (Service-Record $role)) { [void](Invoke-Checked $sc @('delete',$names[$role])) }
        }
        foreach ($path in @($httpConfig,$phpConfig,$proxyPath,$bootstrapGate)) {
            if ($null -ne (Existing $path)) { [IO.File]::Delete($path) }
        }
        Write-Output "Retained controller state, installed ownership information, and rollback slots under $data and $releases."
        return
    }

    $newManifest = Digest (Join-Path $stage 'controller-bundle.json')
    $selected = @($receipt.slots | Where-Object { $_.archive -ceq $Sha256 })
    if ($selected.Count -eq 1 -and $selected[0].manifest -cne $newManifest) { throw 'Retained candidate manifest differs' }
    if (-not $selected.Count -and @($receipt.slots | Where-Object { $_.manifest -ceq $newManifest }).Count) {
        throw 'Equivalent release has a different archive identity'
    }
    $slot = Join-Path $releases $Sha256
    if ($priorProgram -ceq $Sha256) { $candidate = $program }
    elseif ($null -ne (Existing $slot)) {
        if (-not $selected.Count) { throw 'Unowned retained controller release' }
        $candidate = $slot
    } else {
        Install-SlotAcl $stage
        $candidate = $stage
    }
    $manifest = Assert-Slot $candidate $newManifest
    $candidateExe = Join-Path $candidate 'bin\wsprrypi.exe'
    if ((Invoke-Checked $candidateExe @('--storage-format')).Trim() -cne $manifest.storage -or
        (Invoke-Checked $candidateExe @('--artifact-identity')).Trim() -cne
        ('wsprrypi-controller/1 {0} {1} {2} {3}' -f $manifest.binary.source_commit, $manifest.binary.application_version,
            $manifest.binary.tracked_source_dirty.ToString().ToLowerInvariant(), $manifest.binary.controller_inputs_sha256)) {
        throw 'Selected executable cannot verify its controller storage and build identity'
    }

    Quiesce
    if (-not $selected.Count) {
        $receipt.slots += [pscustomobject]@{archive=$Sha256;manifest=$newManifest}
        Save-Receipt $receipt
    }
    if ($priorProgram -cne $Sha256) {
        if ($candidate -ceq $stage) {
            [IO.Directory]::Move($stage, $slot)
            $stage = $null
        }
        Archive-Active $receipt
        [IO.Directory]::Move($slot, $program)
    }
    $defaultAcl = 'D:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)'
    foreach ($role in $names.Keys) {
        [void](Invoke-Checked $sc @('sidtype',$names[$role],'restricted'))
        $acl = $defaultAcl
        if ($role -eq 'Controller') { $acl += "(A;;CCLCRPWP;;;$($ids.Maintenance))" }
        if ($role -eq 'Http') { $acl += "(A;;CCLCCR;;;$($ids.Maintenance))" }
        [void](Invoke-Checked $sc @('sdset',$names[$role],$acl))
    }
    if ($null -eq (Existing $backendState)) { Provision-State initialize }
    $listenerPolicy = New-Policy $false $ids.Controller @() $false
    if ($null -ne (Existing $listenerResult)) {
        Assert-Policy $listenerResult $false $listenerPolicy
        [IO.File]::Delete($listenerResult)
    }
    Provision-State inspect
    Assert-Policy $listenerResult $false $listenerPolicy
    $listeners = Read-Json $listenerResult 4096
    Assert-Fields $listeners @('schema','device_id','role','web_port','socket_port')
    if ($listeners.schema -cne 'listener-bootstrap/1' -or $listeners.role -cne 'controller' -or
        $listeners.device_id -cnotmatch '^[0-9a-f]{32}$' -or $listeners.web_port -isnot [int] -or
        $listeners.socket_port -isnot [int] -or $listeners.web_port -lt 1024 -or $listeners.web_port -gt 49151 -or
        $listeners.socket_port -lt 1024 -or $listeners.socket_port -gt 49151 -or
        $listeners.web_port -eq $listeners.socket_port) {
        throw 'Invalid controller listener bootstrap'
    }
    [IO.File]::Delete($listenerResult)
    Write-Hosting $listeners.web_port $listeners.socket_port
    # Publish the selected slot while services are stopped. Running native
    # owners pin their state ancestry against replacement. Automatic startup
    # is enabled separately, after readiness through the installed endpoint.
    $receipt.active = $Sha256
    Save-Receipt $receipt
    $started = [Collections.Generic.List[string]]::new()
    try {
        foreach ($role in @('Controller','Maintenance','Web','Http')) {
            $started.Add($role)
            Start-Service -Name $names[$role]
            [void](Wait-Service $role 'Running')
            if ($role -eq 'Controller') { Wait-ControllerEndpoint $listeners.device_id }
        }
        Wait-Ready $manifest
        foreach ($role in @('Controller','Web','Http')) { Set-Service -Name $names[$role] -StartupType Automatic }
        # Start the controller first at boot while retaining independent SCM
        # lifecycle authority for maintenance during a backend restore.
        [void](Invoke-Checked $sc @('config',$names.Maintenance,'start=','delayed-auto'))
    } catch {
        $started.Reverse()
        foreach ($role in $started) {
            Set-Service -Name $names[$role] -StartupType Manual
            Stop-Role $role
        }
        throw
    }
    Write-Output "Installed controller $Sha256 on Windows x64."
} finally {
    if ($stage -and $null -ne (Existing $stage)) { [IO.Directory]::Delete($stage, $true) }
    $lock.Dispose()
}
