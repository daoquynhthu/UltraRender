@echo off
echo [Build] Setting up environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo [Build] Cleaning up old build directories...
if exist build_gpu rmdir /s /q build_gpu
if exist CMakeCache.txt del CMakeCache.txt
if exist CMakeFiles rmdir /s /q CMakeFiles

echo [Build] Configuring CMake with CUDA support...
cmake -S . -B build_gpu -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=native

if %errorlevel% neq 0 (
    echo [Build] CMake configuration failed!
    exit /b %errorlevel%
)

echo [Build] Building Project...
cmake --build build_gpu --config Release --parallel

if %errorlevel% neq 0 (
    echo [Build] Build failed!
    exit /b %errorlevel%
)

echo [Build] Build successful!
echo [Build] Executable should be in build_gpu\Release\UltraRender.exe
