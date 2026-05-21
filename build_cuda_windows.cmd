@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo Failed to initialize Visual Studio C++ environment.
    exit /b 1
)

cmake --preset nmake-cuda-release --fresh
if errorlevel 1 (
    echo CUDA configure failed.
    exit /b 1
)

cmake --build --preset nmake-cuda-release
if errorlevel 1 (
    echo CUDA build failed.
    exit /b 1
)

echo CUDA build completed successfully.
exit /b 0
