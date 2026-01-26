$exe = "build\bin\Release\UltraRender.exe"
$scene = "scenes\test_sss.scene"
$output_dir = "output"

if (!(Test-Path $output_dir)) {
    New-Item -ItemType Directory -Path $output_dir
}

$spp_list = 100, 200, 300

foreach ($spp in $spp_list) {
    $filename = "test_sss_${spp}.bmp"
    $check_path = "$output_dir\$filename"
    Write-Host "Rendering with SPP $spp..."
    
    # Measure-Command {
        & $exe --scene $scene --spp $spp --output $filename
    # }
    
    if (Test-Path $check_path) {
        Write-Host "Success: $check_path created."
    } else {
        Write-Host "Error: Failed to create $check_path"
    }
}

Write-Host "Benchmark completed."
