import os
from moviepy import *

image_folder = 'output/glass_collision'
video_name = 'output/glass_collision/glass_collision.mp4'
audio_file = 'output/glass_collision/glass_collision.wav'
fps = 60

if not os.path.exists(image_folder):
    print(f"Error: Folder {image_folder} does not exist.")
    exit(1)

# Get images
images = [img for img in os.listdir(image_folder) if img.endswith(".bmp")]
try:
    images.sort(key=lambda x: int(x.split('_')[1].split('.')[0]))
except Exception as e:
    print(f"Warning: Sorting failed: {e}. Using default order.")
    images.sort()

if not images:
    print("No images found in", image_folder)
    exit()

image_paths = [os.path.join(image_folder, img) for img in images]

print(f"Generating video {video_name} from {len(images)} frames...")

# Create Video Clip
clip = ImageSequenceClip(image_paths, fps=fps)

# Add Audio
if os.path.exists(audio_file):
    print(f"Found audio file: {audio_file}")
    try:
        audio = AudioFileClip(audio_file)
        # Check duration
        print(f"Video Duration: {clip.duration}s, Audio Duration: {audio.duration}s")
        if audio.duration > clip.duration:
            audio = audio.subclipped(0, clip.duration)
        
        clip = clip.with_audio(audio)
        print("Audio merged.")
    except Exception as e:
        print(f"Error merging audio: {e}")
else:
    print(f"Audio not found: {audio_file}")

# Write Output
# libx264 for MP4 is standard. 
# If ffmpeg is strictly missing from system but imageio has it, this should work.
clip.write_videofile(video_name, codec='libx264', audio_codec='aac', fps=fps)
print(f"Video generated successfully: {video_name}")
