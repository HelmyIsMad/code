import serial
import wave
import sys

# --- CONFIGURATION ---
PORT = 'COM3'  # <--- CHANGE THIS to your Black Pill's COM port
BAUD = 500000
OUTPUT_FILE = "stm32_audio.wav"
SAMPLE_RATE = 14000 
# ---------------------

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"Connected to {PORT}. Recording...")
except:
    print(f"Could not open {PORT}. Is the Serial Monitor open in Arduino? Close it first!")
    sys.exit()

# Setup WAV file
wav_file = wave.open(OUTPUT_FILE, 'wb')
wav_file.setnchannels(1)     # Mono
wav_file.setsampwidth(2)     # 16-bit (2 bytes)
wav_file.setframerate(SAMPLE_RATE)

samples_captured = 0

try:
    while True:
        # Read 2 bytes (one 16-bit sample)
        raw_data = ser.read(2)
        if len(raw_data) == 2:
            wav_file.writeframes(raw_data)
            samples_captured += 1
            if samples_captured % (SAMPLE_RATE // 2) == 0:
                print(f"Captured {samples_captured // SAMPLE_RATE} seconds...")

except KeyboardInterrupt:
    print("\nStopping...")
finally:
    wav_file.close()
    ser.close()
    print(f"File saved as {OUTPUT_FILE}")