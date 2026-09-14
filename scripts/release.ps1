<#
.SYNOPSIS
  Spusti kompletni release firmware jednim prikazem.

.DESCRIPTION
  Nastavi FIRMWARE_VERSION v src/OtaConfig.h, commitne, vytvori git tag
  vX.Y.Z a (po potvrzeni) ho pushne. Push tagu spusti GitHub Actions workflow
  (.github/workflows/release.yml), ktery firmware sam zkompiluje (PlatformIO),
  spocita SHA-256 a vytvori GitHub Release se vsemi potrebnymi assety.

  Tento skript sam o sobe nic nekompiluje ani nevytvari Release - to dela
  az CI po pushnuti tagu. Skript jen zjednodusuje rucni kroky (uprava verze,
  commit, tag, push), ktere by jinak clovek delal postupne rucne.

  Adaptovano z https://github.com/sob2008/esp-ota (scripts/release.ps1.template) -
  cesta ke config souboru je pevne "src/OtaConfig.h" (tento projekt ma jen
  jeden sketch/firmware, ne monorepo s vice sketchi).

  POZNAMKA: pokud tento skript spoustite z neinteraktivniho prostredi (napr.
  z Claude Code / jineho AI agenta bez terminalu), potvrzovaci Read-Host
  vyhodi vyjimku "NonInteractive mode" PO tom, co uz commit a tag existuji
  lokalne - push je pak nutne dokoncit rucne (git push origin HEAD; git push
  origin vX.Y.Z).

.PARAMETER Version
  Nova verze firmware ve tvaru MAJOR.MINOR.PATCH, napr. 1.2.0 (bez "v").

.EXAMPLE
  .\scripts\release.ps1 -Version 1.1.0
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Version
)

$ErrorActionPreference = "Stop"

function Fail($msg) {
    Write-Host "CHYBA: $msg" -ForegroundColor Red
    exit 1
}

# Nativni prikazy (git) v PowerShellu nehazi vyjimku pri nenulovem exit
# kodu, takze $ErrorActionPreference = "Stop" je samo o sobe nezachyti -
# napr. odmitnuty commit kvuli pre-commit hooku by jinak tise propadl az
# ke `git tag`/`git push`. Kazde volani, na kterem zalezi, proto jde pres
# tohle a exit kod se overuje rucne.
function Invoke-GitOrFail {
    param([Parameter(Mandatory = $true)][string[]]$GitArgs)
    & git @GitArgs
    if ($LASTEXITCODE -ne 0) {
        Fail "'git $($GitArgs -join ' ')' selhalo (exit kod $LASTEXITCODE)."
    }
}

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Fail "Verze musi byt ve tvaru MAJOR.MINOR.PATCH, napr. 1.2.0 (zadano: '$Version')"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$relConfigPath = "src/OtaConfig.h"
$configPath = Join-Path $repoRoot $relConfigPath

if (-not (Test-Path $configPath)) {
    Fail "Nenalezen $configPath - je skript ve slozce 'scripts' v korenu repozitare?"
}

Push-Location $repoRoot
try {
    # --- Over cisty pracovni strom (krome souboru, ktery za chvili zmenime) ---
    $status = git status --porcelain | Where-Object { $_ -notmatch [regex]::Escape($relConfigPath) }
    if ($status) {
        Write-Host "Pracovni strom ma necekane zmeny:" -ForegroundColor Yellow
        $status | ForEach-Object { Write-Host "  $_" }
        $ans = Read-Host "Pokracovat i tak? (a/N)"
        if ($ans -ne "a") { Fail "Zruseno uzivatelem." }
    }

    $tag = "v$Version"
    if (git tag --list $tag) {
        Fail "Tag $tag uz existuje. Zvol jinou verzi, nebo smaz existujici tag (git tag -d $tag)."
    }

    # --- Nastavit FIRMWARE_VERSION v src/OtaConfig.h (bez BOM, at se nic nemeni v kodovani souboru) ---
    $content = [System.IO.File]::ReadAllText($configPath)
    $pattern = '#define FIRMWARE_VERSION "[^"]*"'
    if ($content -notmatch $pattern) {
        Fail "V $configPath nebyl nalezen radek '#define FIRMWARE_VERSION `"...`"'."
    }
    $newContent = [regex]::Replace($content, $pattern, "#define FIRMWARE_VERSION `"$Version`"")
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($configPath, $newContent, $utf8NoBom)
    Write-Host "FIRMWARE_VERSION nastavena na $Version" -ForegroundColor Green

    # --- Commit + tag ---
    Invoke-GitOrFail @("add", "--", $relConfigPath)
    $diff = git diff --cached --stat
    if (-not $diff) {
        Write-Host "FIRMWARE_VERSION uz na $Version byla nastavena drive, commit se preskakuje." -ForegroundColor Yellow
    } else {
        Invoke-GitOrFail @("commit", "-m", "Release $tag")
    }
    Invoke-GitOrFail @("tag", $tag)

    Write-Host ""
    Write-Host "Pripraveno k odeslani:"
    Write-Host "  posledni commit: $(git log -1 --oneline)"
    Write-Host "  tag:             $tag"
    Write-Host ""

    $confirm = Read-Host "Pushnout commit + tag a spustit CI build/release? (a/N)"
    if ($confirm -ne "a") {
        Write-Host "Zruseno - commit a tag zustavaji jen lokalne."
        Write-Host "Pro uklizeni: git tag -d $tag"
        exit 0
    }

    Invoke-GitOrFail @("push", "origin", "HEAD")
    Invoke-GitOrFail @("push", "origin", $tag)

    $remoteUrl = git config --get remote.origin.url
    Write-Host ""
    Write-Host "Hotovo. Tag $tag byl pushnut, GitHub Actions by mel zacit build za par vterin." -ForegroundColor Green
    Write-Host "Sleduj prubeh v zalozce 'Actions' repozitare: $remoteUrl"
}
finally {
    Pop-Location
}
