import subprocess
import sys
import os
import time

def main():
    # Get the directory of this script
    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_root = os.path.dirname(script_dir)

    # Path to flash.bat
    flash_bat = os.path.join(script_dir, 'flash.bat')

    # Path to read_audio.py
    read_audio_py = os.path.join(script_dir, 'read_audio.py')

    print("Running flash.bat...")
    try:
        result = subprocess.run([flash_bat], cwd=workspace_root, check=True, shell=True)
        print("Flash completed successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Flash failed with return code {e.returncode}")
        sys.exit(1)

    print("Running read_audio.py...")
    time.sleep(2)  # Optional: wait a moment before starting to read audio
    try:
        import read_audio
        print("Audio reading completed successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Audio reading failed with return code {e.returncode}")
        sys.exit(1)

if __name__ == '__main__':
    main()