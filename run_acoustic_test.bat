@echo off
echo Running Acoustic Test Simulation...
bin\UltraRender.exe --scene scenes/acoustic_test.scene --width 800 --height 600 --spp 32 --output acoustic_test.bmp
if %errorlevel% neq 0 (
    echo Simulation Failed!
    exit /b %errorlevel%
)
echo Simulation Complete.
python generate_video_acoustic.py --dir output/acoustic_test --name acoustic_test
