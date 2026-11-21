@echo off
setlocal enabledelayedexpansion

rem ============================================================
rem  Windows release build script
rem  Produces app\build\outputs\apk\release\app-release.apk with
rem  resources\Data packed as one STORE-only assets/data.zip
rem  (injected after packageRelease, then zipalign + apksigner).
rem  Debug builds are untouched: use gradlew.bat assembleDebug.
rem ============================================================

set "ROOT=%~dp0"
cd /d "%ROOT%"

rem --- JDK 17 (must match the client build) ---
set "JAVA_HOME=D:\AndroidDependency\.jdks\jbr-17.0.14"
if not exist "%JAVA_HOME%\bin\java.exe" (
    echo [ERROR] JDK 17 not found: %JAVA_HOME%
    exit /b 1
)
set "PATH=%JAVA_HOME%\bin;%PATH%"

echo [INFO] JAVA_HOME = %JAVA_HOME%
echo [INFO] Building release (assembleRelease + data.zip injection) ...
echo.

call gradlew.bat assembleRelease %*
if errorlevel 1 (
    rem D8 can choke on stale incremental javac/kotlin nest-mates; clear and retry once.
    echo [WARN] First attempt failed; clearing release dex inputs and retrying ...
    rmdir /s /q "app\build\tmp\kotlin-classes\release" 2>nul
    rmdir /s /q "app\build\intermediates\javac\release" 2>nul
    rmdir /s /q "app\build\kotlinToolingMetadata" 2>nul
    call gradlew.bat assembleRelease %*
    if errorlevel 1 (
        echo [FAIL] Release build did not succeed.
        exit /b 1
    )
)

echo.
echo [OK] Release APK: %ROOT%app\build\outputs\apk\release\app-release.apk
echo [NOTE] Install with: adb install -r -g app\build\outputs\apk\release\app-release.apk
endlocal