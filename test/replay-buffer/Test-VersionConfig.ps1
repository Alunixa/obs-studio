param(
    [Parameter(Mandatory)][string]$Directory,
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
$Root = Join-Path $Directory ("obs-version-tests-" + [guid]::NewGuid().ToString('N'))
$Source = Join-Path $Root 'source'
$null = New-Item -ItemType Directory -Path $Source -Force
$VersionModule = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../cmake/common/versionconfig.cmake')).Path
$VersionModule = $VersionModule.Replace('\', '/')
$Fixture = @'
cmake_minimum_required(VERSION 3.28)
set(_obs_default_version 0 0 1)
set(_obs_release_candidate 0)
set(_obs_beta 0)
include("@VERSION_MODULE@")
project(version-probe VERSION ${OBS_VERSION_CANONICAL} LANGUAGES NONE)
if(NOT OBS_VERSION_CANONICAL STREQUAL EXPECTED_VERSION)
  message(FATAL_ERROR "Unexpected canonical version: ${OBS_VERSION_CANONICAL}")
endif()
message(STATUS "PASS version ${OBS_VERSION} -> ${OBS_VERSION_CANONICAL}")
'@
$Fixture.Replace('@VERSION_MODULE@', $VersionModule) |
    Set-Content -LiteralPath (Join-Path $Source 'CMakeLists.txt') -Encoding utf8
git -C $Source init --quiet
if ($LASTEXITCODE) { throw 'Version test git init failed' }
git -C $Source add -- CMakeLists.txt
if ($LASTEXITCODE) { throw 'Version test git add failed' }
git -C $Source -c user.name=VersionTest -c user.email=version-test@example.invalid commit --quiet -m fixture
if ($LASTEXITCODE) { throw 'Version test commit failed' }
git -C $Source tag alunixa-custom-archive
if ($LASTEXITCODE) { throw 'Version test custom tag failed' }
& $CMake -S $Source -B (Join-Path $Root 'non-numeric-tag') '-DEXPECTED_VERSION=0.0.1'
if ($LASTEXITCODE) { throw 'Non-numeric tag fallback failed' }
git -C $Source tag 32.2.2
if ($LASTEXITCODE) { throw 'Version test semantic tag failed' }
& $CMake -S $Source -B (Join-Path $Root 'semantic-tag') '-DEXPECTED_VERSION=32.2.2'
if ($LASTEXITCODE) { throw 'Semantic version tag failed' }
& $CMake -S $Source -B (Join-Path $Root 'override') '-DEXPECTED_VERSION=32.2.2' `
    '-DOBS_VERSION_OVERRIDE=32.2.2-alunixa.20260910.2'
if ($LASTEXITCODE) { throw 'Custom release version override failed' }
Write-Output 'ALL VERSION CONFIGURATION REGRESSIONS PASSED'
