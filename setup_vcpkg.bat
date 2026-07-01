@echo off
rem ============================================================================
rem  setup_vcpkg.bat - bootstrap vcpkg and configure CMake with OpenUSD.
rem
rem  OpenUSD (and the Vulkan validation layer) are delivered via the vcpkg.json
rem  manifest in this directory and consumed only by samples that opt in (today
rem  SkeletalAnimHelper; future samples can too - see CMake/usd_options.cmake).
rem  The exact versions are pinned by the vcpkg checkout tag below (VCPKG_TAG)
rem  plus the builtin-baseline in vcpkg.json.
rem
rem  The first configure compiles OpenUSD and its dependency tree (TBB,
rem  OpenSubdiv, boost). That can take 30+ minutes ONCE; the result is stored in
rem  a repo-local binary cache (.vcpkg-cache) and reused by every later build.
rem ============================================================================
setlocal enabledelayedexpansion

set "REPO_ROOT=%~dp0"

rem Pinned vcpkg release tag -> fixed OpenUSD version. Bump deliberately.
set "VCPKG_TAG=2026.06.24"

if "%VCPKG_ROOT%"=="" (
  set "VCPKG_ROOT=%REPO_ROOT%Packages\vcpkg"
)

if not exist "%VCPKG_ROOT%\.git" (
  echo [setup_vcpkg] Cloning vcpkg into "%VCPKG_ROOT%" ...
  git clone https://github.com/microsoft/vcpkg "%VCPKG_ROOT%" || exit /b 1
)

echo [setup_vcpkg] Pinning vcpkg to %VCPKG_TAG% ...
pushd "%VCPKG_ROOT%"
git fetch --tags --quiet
git checkout %VCPKG_TAG% || (popd & exit /b 1)
popd

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  echo [setup_vcpkg] Bootstrapping vcpkg ...
  call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics || exit /b 1
)

rem Repo-local binary cache so the heavy USD build is reused across clean builds.
set "VCPKG_DEFAULT_BINARY_CACHE=%REPO_ROOT%.vcpkg-cache"
if not exist "%VCPKG_DEFAULT_BINARY_CACHE%" mkdir "%VCPKG_DEFAULT_BINARY_CACHE%"
set "VCPKG_BINARY_SOURCES=clear;files,%VCPKG_DEFAULT_BINARY_CACHE%,readwrite;default,readwrite"

rem Pin all manifest versions to the checked-out vcpkg registry (fixed USD).
findstr /c:"builtin-baseline" "%REPO_ROOT%vcpkg.json" >nul 2>&1
if errorlevel 1 (
  echo [setup_vcpkg] Writing builtin-baseline into vcpkg.json ...
  "%VCPKG_ROOT%\vcpkg.exe" x-update-baseline --add-initial-baseline --x-manifest-root="%REPO_ROOT%." || exit /b 1
)

echo.
echo [setup_vcpkg] NOTE: first configure compiles OpenUSD + deps; this can take
echo                30+ minutes once. Cached afterwards in "%VCPKG_DEFAULT_BINARY_CACHE%".
echo.

cmake -S "%REPO_ROOT%." -B "%REPO_ROOT%build" ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows || exit /b 1

echo.
echo [setup_vcpkg] Configure done. Build with:
echo     cmake --build "%REPO_ROOT%build" --config Release --target SkeletalAnimHelper
endlocal
