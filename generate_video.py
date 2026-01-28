import cv2
import os
import glob

image_folder = 'output/glass_cup/physics_demo'
video_name = 'output/glass_cup/physics_demo.avi'

if not os.path.exists(image_folder):
    print(f"Error: Folder {image_folder} does not exist.")
    exit(1)

images = [img for img in os.listdir(image_folder) if img.endswith(".bmp")]
# Sort images by frame number (frame_000.bmp)
try:
    images.sort(key=lambda x: int(x.split('_')[1].split('.')[0]))
except Exception as e:
    print(f"Warning: Sorting failed: {e}. Using default order.")
    images.sort()

if not images:
    print("No images found in", image_folder)
    exit()

# Read first frame to get dimensions
first_frame_path = os.path.join(image_folder, images[0])
frame = cv2.imread(first_frame_path)
if frame is None:
    print(f"Error: Could not read first frame {first_frame_path}")
    exit(1)

height, width, layers = frame.shape

# Define the codec and create VideoWriter object
# MJPG is a safe bet for AVI
fourcc = cv2.VideoWriter_fourcc(*'MJPG') 
video = cv2.VideoWriter(video_name, fourcc, 60.0, (width, height))

print(f"Generating video {video_name} from {len(images)} frames...")

count = 0
for image in images:
    img_path = os.path.join(image_folder, image)
    img = cv2.imread(img_path)
    if img is not None:
        video.write(img)
        count += 1
        if count % 20 == 0:
            print(f"Processed {count} frames...", end='\r')
    else:
        print(f"Warning: Could not read {img_path}")

video.release()
print(f"\nVideo generated successfully: {video_name}")
