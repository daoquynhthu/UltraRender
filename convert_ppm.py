from PIL import Image
import sys
import os

def convert_bmp_to_png(bmp_path, png_path):
    try:
        if not os.path.exists(bmp_path):
            print(f"Error: BMP file not found at {bmp_path}")
            return

        with Image.open(bmp_path) as img:
            img.save(png_path)
            print(f"Successfully converted {bmp_path} to {png_path}")
    except Exception as e:
        print(f"Failed to convert {bmp_path}: {e}")

if __name__ == "__main__":
    # 示例：将 output 目录下的 glass_cup.bmp 转换为 glass_cup.png
    convert_bmp_to_png("output/glass_cup_fix.bmp", "output/glass_cup_fix.png")
    
