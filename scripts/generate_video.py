import os
import sys
import argparse
from moviepy import *
from pathlib import Path

def generate_video(scene_name, fps=60):
    # Setup paths
    base_dir = Path(os.getcwd())
    output_dir = base_dir / "output" / scene_name
    
    if not output_dir.exists():
        print(f"Error: Output directory {output_dir} does not exist.")
        return False

    video_path = output_dir / f"{scene_name}.mp4"
    audio_path = output_dir / f"{scene_name}.wav"
    
    # Fallback audio name
    if not audio_path.exists():
        fallback_audio = output_dir / "physics_demo.wav"
        if fallback_audio.exists():
            audio_path = fallback_audio

    print(f"Processing scene: {scene_name}")
    print(f"Output directory: {output_dir}")

    # Get images
    images = [img for img in os.listdir(output_dir) if img.endswith(".bmp") and img.startswith("frame_")]
    
    if not images:
        print(f"Error: No frame_*.bmp images found in {output_dir}")
        return False

    # Sort images by frame number
    try:
        images.sort(key=lambda x: int(x.split('_')[1].split('.')[0]))
    except Exception as e:
        print(f"Warning: Sorting failed: {e}. Using default alphabetical order.")
        images.sort()

    image_paths = [str(output_dir / img) for img in images]
    print(f"Found {len(images)} frames.")

    try:
        # Create Video Clip
        clip = ImageSequenceClip(image_paths, fps=fps)

        # Add Audio if available
        if audio_path.exists():
            print(f"Found audio file: {audio_path}")
            try:
                audio = AudioFileClip(str(audio_path))
                print(f"Video Duration: {clip.duration:.2f}s, Audio Duration: {audio.duration:.2f}s")
                
                # Handle duration mismatch
                if audio.duration > clip.duration:
                    print("Trimming audio to match video duration.")
                    audio = audio.subclipped(0, clip.duration)
                elif audio.duration < clip.duration:
                    print("Warning: Audio is shorter than video.")
                
                clip = clip.with_audio(audio)
                print("Audio merged.")
            except Exception as e:
                print(f"Error processing audio: {e}")
        else:
            print(f"Warning: Audio file not found at {audio_path}")

        # Write Output
        print(f"Writing video to {video_path}...")
        clip.write_videofile(str(video_path), codec='libx264', audio_codec='aac', fps=fps, logger='bar')
        print(f"Success! Video generated: {video_path}")
        return True

    except Exception as e:
        print(f"Error generating video: {e}")
        return False

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate video from Render Engine output frames.")
    parser.add_argument("scene_name", help="Name of the scene (folder name in output directory)")
    parser.add_argument("--fps", type=int, default=60, help="Frames per second (default: 60)")
    
    args = parser.parse_args()
    
    generate_video(args.scene_name, args.fps)
