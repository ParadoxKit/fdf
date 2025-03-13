@echo off
setlocal enabledelayedexpansion

set configs=          "NoModule-NoComment -DFDF_USE_CPP_MODULES=OFF -DFDF_NO_COMMENTS=ON"
set configs=!configs! "NoModule-Comment   -DFDF_USE_CPP_MODULES=OFF -DFDF_NO_COMMENTS=OFF"
set configs=!configs! "Module-NoComment   -DFDF_USE_CPP_MODULES=ON  -DFDF_NO_COMMENTS=ON"
set configs=!configs! "Module-Comment     -DFDF_USE_CPP_MODULES=ON  -DFDF_NO_COMMENTS=OFF"

for %%i in (!configs!) do (
    for /f "tokens=1,* delims= " %%a in ("%%i") do (
        set name=%%a
        set name=!name:~1!
        set options=%%b
        set options=!options:~0,-1!

        echo -----------------------------
        echo Generating !name!
        echo -----------------------------
        echo(
        cmake -B build/!name! . !options!
        echo(
        echo(
        echo -----------------------------
        echo Building !name!
        echo -----------------------------
        echo(
        cmake --build build/!name!

        pause
        cls
    )
)


for %%i in (!configs!) do (
    for /f "tokens=1,* delims= " %%a in ("%%i") do (
        set name=%%a
        set name=!name:~1!
        set options=%%b
        set options=!options:~0,-1!

        echo -----------------------------
        echo Testing !name!
        echo -----------------------------
        echo(
        ctest -C Debug --test-dir build/!name! --verbose

        pause
        cls
    )
)

endlocal
