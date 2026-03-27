import struct
import sys
import os

def analyze_pixel(r, g, b, name):
    print(f"{name} Pixel (RGB): ({r}, {g}, {b})")
    # Spectrum Calibration Check:
    # If we render a Red Material (1,0,0) with Spectral pipeline, 
    # it should result in a dominant Red channel.
    if r > 150 and g < 100 and b < 100:
        print(f"{name} Result: CORRECT (RED Dominant)")
    elif b > 150 and r < 100 and g < 100:
        print(f"{name} Result: ERROR (BLUE Swap Detected)")
    elif r > 100 and g > 100 and b > 100:
        print(f"{name} Result: NEUTRAL/WHITE")
    else:
        print(f"{name} Result: OTHER")

def analyze_bmp(filepath):
    try:
        if not os.path.exists(filepath):
            print(f"Error: File {filepath} not found")
            return

        with open(filepath, 'rb') as f:
            header = f.read(14)
            if header[:2] != b'BM':
                print("Not a BMP file")
                return

            dib_header = f.read(40)
            width, height = struct.unpack('<ii', dib_header[4:12])
            bpp = struct.unpack('<H', dib_header[14:16])[0]
            print(f"Analyzing {filepath}: Width={width}, Height={height}, BPP={bpp}")

            if bpp != 24:
                print("Only 24-bit BMP supported")
                return

            offset_val = struct.unpack('<I', header[10:14])[0]
            row_size = (width * 3 + 3) & ~3

            # Sample center (Object) and corner (Sky)
            points = [
                (width // 2, height // 2, "Center (Object)"),
                (10, 10, "Corner (Sky)")
            ]

            for x, y, name in points:
                f.seek(offset_val + y * row_size + x * 3)
                b, g, r = struct.unpack('BBB', f.read(3))
                analyze_pixel(r, g, b, name)

    except Exception as e:
        print(f"Error analyzing BMP: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python calibrate_color.py <filepath>")
    else:
        analyze_bmp(sys.argv[1])
