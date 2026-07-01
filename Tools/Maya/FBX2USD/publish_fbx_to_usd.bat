@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================
rem Usage:
rem   publish_fbx_to_usd.bat
rem   publish_fbx_to_usd.bat "D:\custom_config.ini"
rem
rem Required files in the same folder:
rem   publish_fbx_to_usd.bat
rem   publish_fbx_to_usd.py
rem   publish_fbx_to_usd.ini
rem ============================================================

set "SCRIPT_DIR=%~dp0"

if "%~1"=="" (
    set "INI_PATH=%SCRIPT_DIR%publish_fbx_to_usd.ini"
) else (
    set "INI_PATH=%~1"
)

if not exist "%INI_PATH%" (
    echo [ERROR] INI file not found:
    echo %INI_PATH%
    exit /b 1
)

if not exist "%SCRIPT_DIR%publish_fbx_to_usd.py" (
    echo [ERROR] publish_fbx_to_usd.py not found:
    echo %SCRIPT_DIR%publish_fbx_to_usd.py
    exit /b 1
)

rem ------------------------------------------------------------
rem Read simple key=value INI.
rem Comments starting with ; or # are ignored.
rem Sections like [paths] are ignored.
rem ------------------------------------------------------------

for /f "usebackq tokens=1,* delims==" %%A in ("%INI_PATH%") do (
    set "KEY=%%A"
    set "VALUE=%%B"

    rem Trim leading spaces from key.
    for /f "tokens=* delims= " %%K in ("!KEY!") do set "KEY=%%K"

    rem Ignore empty lines.
    if not "!KEY!"=="" (
        rem Ignore sections.
        if not "!KEY:~0,1!"=="[" (
            rem Ignore comments.
            if not "!KEY:~0,1!"==";" (
                if not "!KEY:~0,1!"=="#" (
                    set "!KEY!=!VALUE!"
                )
            )
        )
    )
)

rem ------------------------------------------------------------
rem Validate required parameters.
rem ------------------------------------------------------------

if "%maya_batch%"=="" (
    echo [ERROR] maya_batch is empty in INI.
    exit /b 1
)

if "%fbx_path%"=="" (
    echo [ERROR] fbx_path is empty in INI.
    exit /b 1
)

if "%output_dir%"=="" (
    echo [ERROR] output_dir is empty in INI.
    exit /b 1
)

if not exist "%maya_batch%" (
    echo [ERROR] MayaBatch not found:
    echo %maya_batch%
    exit /b 1
)

if not exist "%fbx_path%" (
    echo [ERROR] FBX file not found:
    echo %fbx_path%
    exit /b 1
)

if not "%clips_json%"=="" (
    if not exist "%clips_json%" (
        echo [ERROR] clips_json not found:
        echo %clips_json%
        exit /b 1
    )
)

rem ------------------------------------------------------------
rem Export config to environment variables for Python runner.
rem ------------------------------------------------------------

set "PUBLISH_SCRIPT_DIR=%SCRIPT_DIR%"
set "PUBLISH_FBX_PATH=%fbx_path%"
set "PUBLISH_OUTPUT_DIR=%output_dir%"
set "PUBLISH_ASSET_NAME=%asset_name%"
set "PUBLISH_ROOT_JOINT=%root_joint%"
set "PUBLISH_MESH_ROOT=%mesh_root%"
set "PUBLISH_CLIPS_JSON=%clips_json%"
set "PUBLISH_CLEAN_ANIM=%clean_anim%"
set "PUBLISH_PREVIEW=%preview%"
set "PUBLISH_KEEP_INTERMEDIATE_FULL_MODEL=%keep_intermediate_full_model%"
set "PUBLISH_REFERENCE_EXAMPLE=%reference_example%"

rem ------------------------------------------------------------
rem Generate temporary Python runner.
rem ------------------------------------------------------------

set "RUNNER=%TEMP%\maya_publish_fbx_to_usd_%RANDOM%.py"

> "%RUNNER%" echo import os
>> "%RUNNER%" echo import sys
>> "%RUNNER%" echo import importlib
>> "%RUNNER%" echo.
>> "%RUNNER%" echo def none_if_empty(value):
>> "%RUNNER%" echo     value = value or ""
>> "%RUNNER%" echo     return value if value.strip() else None
>> "%RUNNER%" echo.
>> "%RUNNER%" echo def bool_from_ini(value, default=False):
>> "%RUNNER%" echo     value = (value or "").strip().lower()
>> "%RUNNER%" echo     if value in ("1", "true", "yes", "on"):
>> "%RUNNER%" echo         return True
>> "%RUNNER%" echo     if value in ("0", "false", "no", "off"):
>> "%RUNNER%" echo         return False
>> "%RUNNER%" echo     return default
>> "%RUNNER%" echo.
>> "%RUNNER%" echo script_dir = os.environ["PUBLISH_SCRIPT_DIR"]
>> "%RUNNER%" echo sys.path.insert(0, script_dir)
>> "%RUNNER%" echo.
>> "%RUNNER%" echo import publish_fbx_to_usd as p
>> "%RUNNER%" echo importlib.reload(p)
>> "%RUNNER%" echo.
>> "%RUNNER%" echo p.publish_fbx_to_usd(
>> "%RUNNER%" echo     fbx_path=os.environ["PUBLISH_FBX_PATH"],
>> "%RUNNER%" echo     output_dir=os.environ["PUBLISH_OUTPUT_DIR"],
>> "%RUNNER%" echo     asset_name=none_if_empty(os.environ.get("PUBLISH_ASSET_NAME", "")),
>> "%RUNNER%" echo     root_joint=none_if_empty(os.environ.get("PUBLISH_ROOT_JOINT", "")),
>> "%RUNNER%" echo     mesh_root=none_if_empty(os.environ.get("PUBLISH_MESH_ROOT", "")),
>> "%RUNNER%" echo     clips_json=none_if_empty(os.environ.get("PUBLISH_CLIPS_JSON", "")),
>> "%RUNNER%" echo     clean_anim=bool_from_ini(os.environ.get("PUBLISH_CLEAN_ANIM", "true"), True),
>> "%RUNNER%" echo     make_previews=bool_from_ini(os.environ.get("PUBLISH_PREVIEW", "false"), False),
>> "%RUNNER%" echo     keep_intermediate_full_model=bool_from_ini(os.environ.get("PUBLISH_KEEP_INTERMEDIATE_FULL_MODEL", "false"), False),
>> "%RUNNER%" echo     make_reference_example=bool_from_ini(os.environ.get("PUBLISH_REFERENCE_EXAMPLE", "false"), False),
>> "%RUNNER%" echo )

set "RUNNER_MAYA=%RUNNER:\=/%"

echo [INFO] INI: %INI_PATH%
echo [INFO] MayaBatch: %maya_batch%
echo [INFO] FBX: %fbx_path%
echo [INFO] Output: %output_dir%
echo [INFO] Asset: %asset_name%
echo [INFO] Root Joint: %root_joint%
echo [INFO] Mesh Root: %mesh_root%
echo [INFO] Clips JSON: %clips_json%
echo [INFO] Preview: %preview%
echo [INFO] Reference Example: %reference_example%
echo.

"%maya_batch%" -batch -command "python(\"exec(open(r'%RUNNER_MAYA%').read())\")"

set "EXIT_CODE=%ERRORLEVEL%"

if exist "%RUNNER%" del "%RUNNER%"

if not "%EXIT_CODE%"=="0" (
    echo.
    echo [ERROR] MayaBatch failed. Exit code: %EXIT_CODE%
    exit /b %EXIT_CODE%
)

echo.
echo [INFO] Publish finished.
exit /b 0