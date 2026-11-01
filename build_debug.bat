@echo off
chcp 65001 >nul
setlocal

rem 无论从双击还是哪个目录调用，都在客户端工程根目录里执行
cd /d "%~dp0"

rem 客户端固定用 jbr-17（AGP 8.1 + jvmTarget 17）；系统里没配 JAVA_HOME，必须自己指定
if exist "D:\AndroidDependency\.jdks\jbr-17.0.14\bin\javac.exe" set "JAVA_HOME=D:\AndroidDependency\.jdks\jbr-17.0.14"
set "PATH=%JAVA_HOME%\bin;%PATH%"

set "APK=%~dp0app\build\outputs\apk\debug\app-debug.apk"

echo [build_debug] JAVA_HOME=%JAVA_HOME%
echo [build_debug] gradlew assembleDebug %*

call gradlew.bat assembleDebug %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo [build_debug] 构建失败，Gradle 退出码 %RC%
    endlocal
    exit /b %RC%
)

echo [build_debug] 构建成功
for %%F in ("%APK%") do echo [build_debug] %%~fF  %%~zF 字节  %%~tF
echo [build_debug] 装机： adb install -r "%APK%"

endlocal
