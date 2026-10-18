[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$TargetModDir = "",

    [Parameter(Mandatory=$false)]
    [string]$CrysisDir = "",

    [Parameter(Mandatory=$false)]
    [string]$Repo = "salaza/crysis1_coop",

    [Parameter(Mandatory=$false)]
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Get-GitHubToken {
    # 1. Environment variable
    if ($env:GITHUB_TOKEN) {
        return $env:GITHUB_TOKEN.Trim()
    }

    # 2. Local token file in script dir or target mod dir
    $tokenPaths = @(
        "$PSScriptRoot\github_token.txt",
        "$TargetModDir\github_token.txt",
        "$CrysisDir\github_token.txt"
    )
    foreach ($p in $tokenPaths) {
        if ($p -and (Test-Path $p)) {
            $t = (Get-Content -Path $p -Raw).Trim()
            if ($t) { return $t }
        }
    }

    # 3. Git Credential Manager
    try {
        $gitCmd = Get-Command "git" -ErrorAction SilentlyContinue
        if ($gitCmd) {
            $credInput = "protocol=https`nhost=github.com`n`n"
            $creds = $credInput | git credential fill 2>$null
            $passLine = $creds | Where-Object { $_ -match "^password=" }
            if ($passLine) {
                $t = ($passLine -replace "^password=", "").Trim()
                if ($t) { return $t }
            }
        }
    } catch {}

    return ""
}

# Resolve directories if not provided
if (-not $TargetModDir) {
    if ($CrysisDir) {
        $TargetModDir = Join-Path $CrysisDir "Mods\CrysisCoop"
    } else {
        $TargetModDir = $PSScriptRoot
    }
}

if (-not (Test-Path $TargetModDir)) {
    New-Item -ItemType Directory -Path $TargetModDir -Force | Out-Null
}

$versionFile = Join-Path $TargetModDir "version.txt"
$localVersion = "none"
if (Test-Path $versionFile) {
    $localVersion = (Get-Content -Path $versionFile -Raw).Trim()
}

$token = Get-GitHubToken

function Fetch-LatestRelease([string]$authToken) {
    $headers = @{
        "User-Agent" = "CrysisCoop-Updater"
        "Accept" = "application/vnd.github.v3+json"
    }
    if ($authToken) {
        $headers["Authorization"] = "Bearer $authToken"
    }
    return Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -Headers $headers -Method Get
}

$release = $null
try {
    $release = Fetch-LatestRelease $token
} catch {
    $errStr = $_.ToString()
    # If 401 or 404, we likely need a token for this private repo
    if (-not $token) {
        Write-Host ""
        Write-Host "[!] Repository '$Repo' requires GitHub authentication." -ForegroundColor Yellow
        Write-Host "    Please enter your GitHub Personal Access Token (PAT):" -ForegroundColor Cyan
        $enteredToken = (Read-Host "GitHub Token").Trim()
        if ($enteredToken) {
            $token = $enteredToken
            $tokenSavePath = Join-Path $TargetModDir "github_token.txt"
            Set-Content -Path $tokenSavePath -Value $token -Force
            Write-Host "    Token saved to: $tokenSavePath" -ForegroundColor Gray
            $release = Fetch-LatestRelease $token
        } else {
            Write-Warning "No token provided. Skipping release update."
            exit 0
        }
    } else {
        Write-Warning "Could not fetch release from GitHub: $errStr"
        exit 0
    }
}

if (-not $release -or -not $release.tag_name) {
    Write-Warning "No valid release found on GitHub repository $Repo."
    exit 0
}

$remoteTag = $release.tag_name.Trim()
$bin64File = Join-Path $TargetModDir "Bin64\CrysisCoop.dll"
$bin32File = Join-Path $TargetModDir "Bin32\CrysisCoop.dll"
$filesExist = (Test-Path $bin64File) -and (Test-Path $bin32File)

if (-not $Force -and ($localVersion -eq $remoteTag) -and $filesExist) {
    Write-Host "[Updater] Crysis Co-op is up to date ($remoteTag)." -ForegroundColor Green
    exit 0
}

Write-Host "[Updater] New version available: $remoteTag (Installed: $localVersion)" -ForegroundColor Cyan
$asset = $release.assets | Where-Object { $_.name -like "*.zip" } | Select-Object -First 1

if (-not $asset) {
    Write-Warning "No .zip asset found in release $remoteTag."
    exit 0
}

Write-Host "[Updater] Downloading $($asset.name) ($([math]::Round($asset.size / 1MB, 2)) MB)..." -ForegroundColor Yellow

$tempZip = Join-Path ([System.IO.Path]::GetTempPath()) "CrysisCoop_$remoteTag.zip"
if (Test-Path $tempZip) { Remove-Item -Force $tempZip }

# Download asset (handling private repo 302 redirect to S3)
$downloadSuccess = $false

if ($token) {
    try {
        $req = [System.Net.HttpWebRequest]::Create($asset.url)
        $req.Headers.Add("Authorization", "Bearer $token")
        $req.UserAgent = "CrysisCoop-Updater"
        $req.Accept = "application/octet-stream"
        $req.AllowAutoRedirect = $false

        $s3Url = $null
        try {
            $resp = $req.GetResponse()
            if ([int]$resp.StatusCode -eq 302) {
                $s3Url = $resp.GetResponseHeader("Location")
            }
            $resp.Close()
        } catch [System.Net.WebException] {
            $resp = $_.Exception.Response
            if ($resp) {
                if ([int]$resp.StatusCode -eq 302) {
                    $s3Url = $resp.GetResponseHeader("Location")
                }
                $resp.Close()
            }
        }

        if ($s3Url) {
            Invoke-WebRequest -Uri $s3Url -OutFile $tempZip -UserAgent "CrysisCoop-Updater"
            $downloadSuccess = $true
        }
    } catch {
        Write-Warning "Direct API download failed: $_"
    }
}

if (-not $downloadSuccess -and $asset.browser_download_url) {
    try {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tempZip -UserAgent "CrysisCoop-Updater"
        $downloadSuccess = $true
    } catch {
        Write-Warning "Browser download failed: $_"
    }
}

if (-not (Test-Path $tempZip) -or (Get-Item $tempZip).Length -eq 0) {
    Write-Warning "Download of release package failed."
    exit 0
}

Write-Host "[Updater] Extracting package to $TargetModDir..." -ForegroundColor Yellow
Expand-Archive -Path $tempZip -DestinationPath $TargetModDir -Force
Remove-Item -Force $tempZip

# Write updated version.txt
Set-Content -Path $versionFile -Value $remoteTag -Force -NoNewline

# Also copy convenience launcher bats to Crysis root if CrysisDir is set and different from TargetModDir
if ($CrysisDir -and (Test-Path $CrysisDir) -and ($CrysisDir -ne $TargetModDir)) {
    $batsToCopy = @("Connect_78.132.76.234.bat", "Join_Coop_Client.bat", "Host_Coop_Server.bat")
    foreach ($bat in $batsToCopy) {
        $srcBat = Join-Path $TargetModDir $bat
        if (Test-Path $srcBat) {
            Copy-Item -Path $srcBat -Destination (Join-Path $CrysisDir $bat) -Force
        }
    }
}

Write-Host "[Updater] Successfully updated Crysis Co-op to $remoteTag!" -ForegroundColor Green
exit 0
