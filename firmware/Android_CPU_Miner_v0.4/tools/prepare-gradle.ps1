$ErrorActionPreference = 'Stop'

# CRSminer Android: build-only wrapper repair, v3 (PowerShell HOME fix).
# Does not modify the app source, mining algorithm, Android resources, or Gradle versions.
# Uses only checksum-verified official Gradle artifacts.
$version = '8.9'
$expectedWrapperSha = '498495120a03b9a6ab5d155f5de3c8f0d986a449153702fb80fc80e134484f17'
$expectedDistributionSha = 'd725d707bfabd4dfdc958c624003b3c80accc03f7037b5122c4b1d0ef15cecab'
$trustedWrapperHashes = @(
    $expectedWrapperSha,
    '2db75c40782f5e8ba1fc278a5574bab070adccb2d21ca5a6e5ed840888448046',
    '497c8c2a7e5031f6aa847f88104aa80a93532ec32d1d2f67a194a9c7'
)
$root = Split-Path -Parent $PSScriptRoot
$wrapperPath = Join-Path $root 'gradle\wrapper\gradle-wrapper.jar'
$originalJavaHome = $env:JAVA_HOME
$originalPath = $env:PATH
$work = Join-Path ([IO.Path]::GetTempPath()) ('crsminer-gradle-repair-' + [guid]::NewGuid().ToString('N'))

function Sha256([string] $path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function IsTrustedWrapper([string] $path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    return ($trustedWrapperHashes -contains (Sha256 $path))
}
function FindGradleExe([string] $directory) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { return $null }
    $found = Get-ChildItem -LiteralPath $directory -Recurse -File -Filter 'gradle.bat' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '[\\/]gradle-8\.9[\\/]bin[\\/]gradle\.bat$' } |
        Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}
function JavaMajorVersion([string] $javaExe) {
    try {
        # java -version writes to stderr; capture it without PowerShell treating
        # stderr as a terminating error under ErrorActionPreference=Stop.
        $start = New-Object System.Diagnostics.ProcessStartInfo
        $start.FileName = $javaExe
        $start.Arguments = '-version'
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardError = $true
        $start.RedirectStandardOutput = $true
        $proc = [System.Diagnostics.Process]::Start($start)
        $versionText = $proc.StandardError.ReadToEnd() + [Environment]::NewLine + $proc.StandardOutput.ReadToEnd()
        $proc.WaitForExit()
        if ($versionText -match '(?:openjdk|java)\s+version\s+"?([0-9]+)(?:\.([0-9]+))?') {
            $major = [int]$Matches[1]
            if ($major -eq 1 -and $Matches[2]) { return [int]$Matches[2] }
            return $major
        }
    } catch {}
    return 0
}
function ChooseCompatibleJavaHome {
    # Prefer Android Studio's embedded JBR, even if system JAVA_HOME/PATH point to Java 25.
    $candidateHomes = New-Object 'System.Collections.Generic.List[string]'
    if ($env:ProgramFiles) {
        $candidateHomes.Add((Join-Path $env:ProgramFiles 'Android\Android Studio\jbr'))
        $candidateHomes.Add((Join-Path $env:ProgramFiles 'Android\Android Studio\jre'))
    }
    if ($env:LOCALAPPDATA) {
        $candidateHomes.Add((Join-Path $env:LOCALAPPDATA 'Programs\Android Studio\jbr'))
    }
    if ($env:JAVA_HOME) { $candidateHomes.Add($env:JAVA_HOME) }
    $knownRoots = @()
    if ($env:ProgramFiles) {
        $knownRoots += (Join-Path $env:ProgramFiles 'Eclipse Adoptium')
        $knownRoots += (Join-Path $env:ProgramFiles 'Java')
        $knownRoots += (Join-Path $env:ProgramFiles 'Microsoft')
        $knownRoots += (Join-Path $env:ProgramFiles 'Zulu')
    }
    foreach ($jdkRoot in $knownRoots) {
        if (-not (Test-Path -LiteralPath $jdkRoot -PathType Container)) { continue }
        Get-ChildItem -LiteralPath $jdkRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $candidateHomes.Add($_.FullName)
        }
    }
    # Never call this variable $home: PowerShell treats it as its read-only $HOME.
    foreach ($jdkHome in $candidateHomes) {
        $javaExe = Join-Path $jdkHome 'bin\java.exe'
        if (-not (Test-Path -LiteralPath $javaExe -PathType Leaf)) { continue }
        $major = JavaMajorVersion $javaExe
        Write-Host ('Found Java ' + $major + ': ' + $jdkHome)
        if ($major -eq 17 -or $major -eq 21 -or $major -eq 22 -or $major -eq 18 -or $major -eq 19 -or $major -eq 20) {
            return @{ Home = $jdkHome; Major = $major }
        }
    }
    return $null
}

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Write-Host 'CRSminer Android v0.4 - Gradle wrapper preparation v3' -ForegroundColor Cyan
    if (IsTrustedWrapper $wrapperPath) {
        Write-Host 'Verified wrapper already exists. No repair necessary.' -ForegroundColor Green
        Write-Host 'IMPORTANT: In Android Studio use Gradle JDK 17 or 21 (not Java 25).'
        exit 0
    }

    $siblingRoot = Split-Path -Parent $root
    foreach ($sibling in (Get-ChildItem -LiteralPath $siblingRoot -Directory -ErrorAction SilentlyContinue)) {
        if ($sibling.FullName -eq $root) { continue }
        $candidate = Join-Path $sibling.FullName 'gradle\wrapper\gradle-wrapper.jar'
        if (IsTrustedWrapper $candidate) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $wrapperPath) | Out-Null
            Copy-Item -LiteralPath $candidate -Destination $wrapperPath -Force
            Write-Host 'Reused an official checksum-verified Gradle wrapper from an adjacent project.' -ForegroundColor Green
            Write-Host 'IMPORTANT: In Android Studio use Gradle JDK 17 or 21 (not Java 25).'
            exit 0
        }
    }

    New-Item -ItemType Directory -Force -Path $work | Out-Null

    # Gradle does NOT publish the wrapper as a standalone distribution JAR.
    # Retrieve Gradle 8.9's committed wrapper JAR from the official Gradle GitHub
    # release tag and validate against its public SHA-256 checksum.
    $downloadUrls = @(
        'https://raw.githubusercontent.com/gradle/gradle/v8.9.0/gradle/wrapper/gradle-wrapper.jar'
    )
    foreach ($url in $downloadUrls) {
        $downloadedJar = Join-Path $work 'gradle-8.9-wrapper.jar'
        try {
            Write-Host ('Trying official wrapper download: ' + $url)
            Invoke-WebRequest -Uri $url -OutFile $downloadedJar -UseBasicParsing -TimeoutSec 120
            $actualSha = Sha256 $downloadedJar
            if ($actualSha -ne $expectedWrapperSha) {
                throw ('Official wrapper checksum mismatch. Got ' + $actualSha)
            }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $wrapperPath) | Out-Null
            Copy-Item -LiteralPath $downloadedJar -Destination $wrapperPath -Force
            Write-Host 'SUCCESS: verified official Gradle 8.9 wrapper JAR installed.' -ForegroundColor Green
            Write-Host 'IMPORTANT: In Android Studio select Gradle JDK 17 or 21 (not Java 25).'
            exit 0
        } catch {
            Write-Host ('Standalone wrapper unavailable: ' + $_.Exception.Message) -ForegroundColor Yellow
            Remove-Item -LiteralPath $downloadedJar -Force -ErrorAction SilentlyContinue
        }
    }

    # Fallback: generate the wrapper from checksum-verified Gradle 8.9 with a
    # compatible Java, never using Java 25 even if it is on the global PATH.
    $selectedJava = ChooseCompatibleJavaHome
    if (-not $selectedJava) {
        throw 'No compatible Java runtime found. Gradle 8.9 supports Java 17 through 22 for execution, not Java 25. In Android Studio, locate the bundled JBR; otherwise install JDK 17 or 21, set JAVA_HOME to its folder and rerun.'
    }
    $env:JAVA_HOME = $selectedJava.Home
    $env:PATH = (Join-Path $selectedJava.Home 'bin') + ';' + $originalPath
    Write-Host ('Using Java ' + $selectedJava.Major + ' for wrapper generation: ' + $selectedJava.Home) -ForegroundColor Green

    $gradleUserHome = if ($env:GRADLE_USER_HOME) { $env:GRADLE_USER_HOME } else { Join-Path $env:USERPROFILE '.gradle' }
    $cachedDistribution = Join-Path $gradleUserHome 'wrapper\dists\gradle-8.9-bin'
    $gradleExe = FindGradleExe $cachedDistribution
    if ($gradleExe) {
        Write-Host 'Found existing Gradle 8.9 installation in user cache.'
    } else {
        $zip = Join-Path $work 'gradle-8.9-bin.zip'
        $distributionUrls = @(
            'https://services.gradle.org/distributions/gradle-8.9-bin.zip',
            'https://downloads.gradle.org/distributions/gradle-8.9-bin.zip'
        )
        $lastError = ''
        foreach ($url in $distributionUrls) {
            try {
                Write-Host ('Downloading official Gradle 8.9: ' + $url)
                Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 900
                if ((Sha256 $zip) -ne $expectedDistributionSha) {
                    throw 'Downloaded distribution failed official SHA-256 verification.'
                }
                Write-Host 'Official Gradle 8.9 distribution checksum verified.' -ForegroundColor Green
                break
            } catch {
                $lastError = $_.Exception.Message
                Write-Host ('Download unavailable: ' + $lastError) -ForegroundColor Yellow
                Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
            }
        }
        if (-not (Test-Path -LiteralPath $zip)) {
            throw ('Could not obtain a verified Gradle 8.9 distribution: ' + $lastError)
        }
        Write-Host 'Extracting verified Gradle distribution...'
        Expand-Archive -LiteralPath $zip -DestinationPath $work -Force
        $gradleExe = Join-Path $work 'gradle-8.9\bin\gradle.bat'
        if (-not (Test-Path -LiteralPath $gradleExe)) { throw 'Missing Gradle executable in verified distribution.' }
    }

    $isolatedProject = Join-Path $work 'wrapper-project'
    New-Item -ItemType Directory -Force -Path $isolatedProject | Out-Null
    Set-Content -LiteralPath (Join-Path $isolatedProject 'settings.gradle') -Encoding ASCII -Value "rootProject.name = 'crsminer-wrapper-bootstrap'"
    Set-Content -LiteralPath (Join-Path $isolatedProject 'build.gradle') -Encoding ASCII -Value ''
    Write-Host 'Generating Gradle 8.9 wrapper with compatible Java...'
    & $gradleExe '--project-dir' $isolatedProject '--offline' '--no-daemon' 'wrapper' '--gradle-version' $version '--distribution-type' 'bin'
    if ($LASTEXITCODE -ne 0) { throw ('Official Gradle wrapper task failed (exit code ' + $LASTEXITCODE + ').') }

    $generatedJar = Join-Path $isolatedProject 'gradle\wrapper\gradle-wrapper.jar'
    if (-not (Test-Path -LiteralPath $generatedJar -PathType Leaf)) {
        throw 'Gradle wrapper task did not create gradle-wrapper.jar.'
    }
    $actual = Sha256 $generatedJar
    if ($actual -ne $expectedWrapperSha) {
        throw ('Wrapper checksum differs from official Gradle 8.9 checksum. Got ' + $actual)
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $wrapperPath) | Out-Null
    Copy-Item -LiteralPath $generatedJar -Destination $wrapperPath -Force
    Write-Host 'SUCCESS: verified Gradle 8.9 wrapper generated.' -ForegroundColor Green
    Write-Host 'IMPORTANT: In Android Studio select Gradle JDK 17 or 21 (not Java 25).'
    exit 0
} catch {
    Write-Host ('PREPARATION FAILED: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    $env:JAVA_HOME = $originalJavaHome
    $env:PATH = $originalPath
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
