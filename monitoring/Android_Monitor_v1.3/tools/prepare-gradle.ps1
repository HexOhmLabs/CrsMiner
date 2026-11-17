$ErrorActionPreference = 'Stop'
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $root = Split-Path -Parent $PSScriptRoot
    $destination = Join-Path $root 'gradle\wrapper\gradle-wrapper.jar'
    # Any verifiably official Gradle wrapper JAR may bootstrap the Gradle 9.5.0 distribution.
    # The wrapper JAR version and distribution version do not have to be identical.
    $officialVersions = @('9.5.0', '9.4.1', '9.5.0-rc-4', '9.5.1')
    $approvedHashes = @{}
    foreach ($version in $officialVersions) {
        $checksumUrl = 'https://services.gradle.org/distributions/gradle-' + $version + '-wrapper.jar.sha256'
        try {
            $response = Invoke-WebRequest -Uri $checksumUrl -UseBasicParsing -TimeoutSec 50
            $checksumText = $response.Content
            if ($checksumText -is [byte[]]) {
                $checksumText = [System.Text.Encoding]::ASCII.GetString($checksumText)
            }
            $hash = (($checksumText.ToString().Trim()) -split '\s+')[0].ToLowerInvariant()
            if ($hash -match '^[a-f0-9]{64}$') { $approvedHashes[$hash] = $version }
        } catch { Write-Host ('Checksum for Gradle ' + $version + ' unavailable: ' + $_.Exception.Message) -ForegroundColor Yellow }
    }
    if ($approvedHashes.Count -eq 0) { throw 'Could not obtain any official wrapper SHA-256 checksums.' }

    if (Test-Path $destination) {
        $oldHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($approvedHashes.ContainsKey($oldHash)) {
            Write-Host 'Correct Gradle wrapper already present.' -ForegroundColor Green
            exit 0
        }
        Write-Host 'Found an unverified wrapper JAR. Removing it.'
        Remove-Item -LiteralPath $destination -Force
    }

    # The Gradle distribution server publishes the official checksum, not the JAR itself.
    # Gradle commits the wrapper JAR in its own GitHub repository.
    $sources = @(
        'https://raw.githubusercontent.com/gradle/gradle/v9.5.0/gradle/wrapper/gradle-wrapper.jar',
        'https://github.com/gradle/gradle/raw/refs/tags/v9.5.0/gradle/wrapper/gradle-wrapper.jar',
        'https://raw.githubusercontent.com/android/snippets/main/gradle/wrapper/gradle-wrapper.jar'
    )
    $verified = $false
    $lastError = ''
    foreach ($url in $sources) {
        try {
            Write-Host 'Fetching the Gradle wrapper from the official Gradle GitHub repository...'
            Invoke-WebRequest -Uri $url -OutFile $destination -UseBasicParsing -TimeoutSec 90
            $downloadedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
            if (-not $approvedHashes.ContainsKey($downloadedHash)) {
                $lastError = 'Wrapper SHA-256 did not match any verified official Gradle checksum.'
                Write-Host $lastError -ForegroundColor Yellow
                Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
                continue
            }
            $verified = $true
            break
        } catch {
            $lastError = $_.Exception.Message
            Write-Host ('Download attempt failed: ' + $lastError) -ForegroundColor Yellow
            Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        }
    }
    if (-not $verified) {
        throw ('Could not retrieve a checksum-verified Gradle wrapper. Last error: ' + $lastError)
    }
    Write-Host 'SUCCESS: Official Gradle wrapper verified by SHA-256.' -ForegroundColor Green
    Write-Host 'Open CRS_Mining_Android in Android Studio and Sync Project with Gradle Files.'
    exit 0
} catch {
    Write-Host ('Error: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
