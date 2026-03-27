import struct
import sys
import os

def analyze_bmp(filepath, x, y):
    try:
        with open(filepath, 'rb') as f:
            header = f.read(14)
            dib_header = f.read(40)
            width, height = struct.unpack('<ii', dib_header[4:12])
            offset_val = struct.unpack('<I', header[10:14])[0]
            row_size = (width * 3 + 3) & ~3
            
            f.seek(offset_val + y * row_size + x * 3)
            b, g, r = struct.unpack('BBB', f.read(3))
            print(f"Pixel at ({x}, {y}): RGB=({r}, {g}, {b})")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    analyze_bmp(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]))
