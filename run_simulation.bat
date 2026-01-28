@echo off
echo Running Glass Collision Simulation...
bin\UltraRender.exe --scene scenes/glass_collision.scene --width 640 --height 480 --spp 32 --output output_collision.bmp
if %errorlevel% neq 0 (
    echo Simulation Failed!
    exit /b %errorlevel%
)
echo Simulation Complete.
