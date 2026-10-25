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

# Sanitize any trailing backslashes or accidentally escaped quotes from batch arguments
if ($TargetModDir) { $TargetModDir = ($TargetModDir -replace '"', '').Trim().TrimEnd('\', '/') }
if ($CrysisDir)    { $CrysisDir    = ($CrysisDir    -replace '"', '').Trim().TrimEnd('\', '/') }

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

function Download-ReleaseAsset($targetAsset, [string]$destinationPath, [string]$authToken) {
    if (Test-Path $destinationPath) { Remove-Item -Force $destinationPath }
    $downloadSuccess = $false

    if ($targetAsset.browser_download_url) {
        try {
            Invoke-WebRequest -Uri $targetAsset.browser_download_url -OutFile $destinationPath -UserAgent "CrysisCoop-Updater"
            if ((Test-Path $destinationPath) -and (Get-Item $destinationPath).Length -gt 0) {
                return $true
            }
        } catch {}
    }

    if ($authToken) {
        try {
            $req = [System.Net.HttpWebRequest]::Create($targetAsset.url)
            $req.Headers.Add("Authorization", "Bearer $authToken")
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
                Invoke-WebRequest -Uri $s3Url -OutFile $destinationPath -UserAgent "CrysisCoop-Updater"
                if ((Test-Path $destinationPath) -and (Get-Item $destinationPath).Length -gt 0) {
                    return $true
                }
            }
        } catch {
            Write-Warning "Direct API download failed: $_"
        }
    }

    return $false
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

# ----------------------------------------------------
# 1. Download & Install Mod Release Package
# ----------------------------------------------------
if (-not $Force -and ($localVersion -eq $remoteTag) -and $filesExist) {
    Write-Host "[Updater] Crysis Co-op is up to date ($remoteTag)." -ForegroundColor Green
} else {
    Write-Host "[Updater] New version available: $remoteTag (Installed: $localVersion)" -ForegroundColor Cyan
    $modAsset = $release.assets | Where-Object { $_.name -like "CrysisCoop_Release*.zip" } | Select-Object -First 1
    if (-not $modAsset) {
        $modAsset = $release.assets | Where-Object { $_.name -like "*.zip" -and $_.name -notlike "*Bin64*" } | Select-Object -First 1
    }

    if (-not $modAsset) {
        Write-Warning "No mod .zip asset found in release $remoteTag."
    } else {
        Write-Host "[Updater] Downloading $($modAsset.name) ($([math]::Round($modAsset.size / 1MB, 2)) MB)..." -ForegroundColor Yellow
        $tempZip = Join-Path ([System.IO.Path]::GetTempPath()) "CrysisCoop_$remoteTag.zip"
        
        $downloadSuccess = Download-ReleaseAsset $modAsset $tempZip $token
        if ($downloadSuccess -and (Test-Path $tempZip)) {
            Write-Host "[Updater] Extracting package to $TargetModDir..." -ForegroundColor Yellow
            Expand-Archive -Path $tempZip -DestinationPath $TargetModDir -Force
            Remove-Item -Force $tempZip -ErrorAction SilentlyContinue
            Set-Content -Path $versionFile -Value $remoteTag -Force -NoNewline
        } else {
            Write-Warning "Download of release package failed."
        }
    }
}

# ----------------------------------------------------
# 2. Check & Install Missing 64-bit Crysis Binaries (Steam Issue)
# ----------------------------------------------------
if ($CrysisDir -and (Test-Path $CrysisDir)) {
    $bin64CrySystem = Join-Path $CrysisDir "Bin64\CrySystem.dll"
    if (-not (Test-Path $bin64CrySystem)) {
        Write-Host ""
        Write-Host "[!] Missing 64-bit Crysis binaries in '$CrysisDir\Bin64'." -ForegroundColor Yellow
        Write-Host "    Downloading official 64-bit patch binaries (Crysis_Bin64_v1.2.1.zip)..." -ForegroundColor Cyan
        
        $bin64Asset = $release.assets | Where-Object { $_.name -like "*Bin64*.zip" } | Select-Object -First 1
        if ($bin64Asset) {
            $tempBin64Zip = Join-Path ([System.IO.Path]::GetTempPath()) "Crysis_Bin64_v1.2.1.zip"
            $dlSuccess = Download-ReleaseAsset $bin64Asset $tempBin64Zip $token
            if ($dlSuccess -and (Test-Path $tempBin64Zip)) {
                Write-Host "    Extracting 64-bit binaries to $CrysisDir..." -ForegroundColor Yellow
                Expand-Archive -Path $tempBin64Zip -DestinationPath $CrysisDir -Force
                Remove-Item -Force $tempBin64Zip -ErrorAction SilentlyContinue
                Write-Host "    [OK] 64-bit Crysis installation patched successfully!" -ForegroundColor Green
            } else {
                Write-Warning "Failed to download 64-bit binaries. The game will run in 32-bit mode."
            }
        }
    }
}

# ----------------------------------------------------
# 3. Check Crysis Patch Version
# ----------------------------------------------------
if ($CrysisDir -and (Test-Path $CrysisDir)) {
    $bin32CrySystem = Join-Path $CrysisDir "Bin32\CrySystem.dll"
    if (Test-Path $bin32CrySystem) {
        $gameVer = (Get-Item $bin32CrySystem).VersionInfo.FileVersion
        if ($gameVer) {
            if ($gameVer -match "6156") {
                Write-Host "[OK] Crysis base installation is on latest official Patch 1.2.1 ($gameVer)." -ForegroundColor Green
            } else {
                Write-Host "[!] Note: Crysis is currently version $gameVer (latest is 1.2.1 / build 6156)." -ForegroundColor Yellow
                Write-Host "    If you encounter crashes with retail DVD versions, patch to 1.2.1." -ForegroundColor Yellow
            }
        }
    }
}

# ----------------------------------------------------
# 4. Copy Convenience Launcher Batch Files to Crysis Root
# ----------------------------------------------------
if ($CrysisDir -and (Test-Path $CrysisDir) -and ($CrysisDir -ne $TargetModDir)) {
    $oldBatRoot = Join-Path $CrysisDir "Connect_78.132.76.234.bat"
    if (Test-Path $oldBatRoot) { Remove-Item -Force $oldBatRoot -ErrorAction SilentlyContinue }

    $batsToCopy = @("Join_Coop_Client.bat", "Host_Coop_Server.bat")
    foreach ($bat in $batsToCopy) {
        $srcBat = Join-Path $TargetModDir $bat
        if (Test-Path $srcBat) {
            Copy-Item -Path $srcBat -Destination (Join-Path $CrysisDir $bat) -Force
        }
    }
}

# ----------------------------------------------------
# 5. Ensure C1-Launcher Fixed Executables are Installed for Raw Clients
# ----------------------------------------------------
if ($CrysisDir -and (Test-Path $CrysisDir)) {
    # Bin32
    $launcher32 = Join-Path $TargetModDir "Launcher\Bin32\Crysis.exe"
    $gameExe32 = Join-Path $CrysisDir "Bin32\Crysis.exe"
    if ((Test-Path $launcher32) -and (Test-Path $gameExe32)) {
        $size32 = (Get-Item $gameExe32).Length
        if ($size32 -gt 500000) {
            Write-Host "[Updater] Installing fixed C1-Launcher into Bin32..." -ForegroundColor Cyan
            Copy-Item $gameExe32 (Join-Path $CrysisDir "Bin32\Crysis_backup.exe") -Force
            Copy-Item $launcher32 $gameExe32 -Force
        }
    }
    # Bin64
    $launcher64 = Join-Path $TargetModDir "Launcher\Bin64\Crysis.exe"
    $bin64Dir = Join-Path $CrysisDir "Bin64"
    $crySys64 = Join-Path $bin64Dir "CrySystem.dll"
    if ((Test-Path $launcher64) -and (Test-Path $crySys64)) {
        $gameExe64 = Join-Path $bin64Dir "Crysis.exe"
        if (-not (Test-Path $gameExe64) -or ((Get-Item $gameExe64).Length -gt 500000)) {
            Write-Host "[Updater] Installing fixed C1-Launcher into Bin64..." -ForegroundColor Cyan
            if (Test-Path $gameExe64) {
                Copy-Item $gameExe64 (Join-Path $bin64Dir "Crysis_backup.exe") -Force
            }
            Copy-Item $launcher64 $gameExe64 -Force
        }
    }
}

# Remove legacy IP connect bat if present in mod folder
$oldBatMod = Join-Path $TargetModDir "Connect_78.132.76.234.bat"
if (Test-Path $oldBatMod) { Remove-Item -Force $oldBatMod -ErrorAction SilentlyContinue }

Write-Host "[Updater] Successfully verified and updated Crysis Co-op!" -ForegroundColor Green
exit 0
