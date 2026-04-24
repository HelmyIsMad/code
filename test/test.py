import serial
import wave
import sys

PORT = 'COM3' 
BAUD = 115200 # Note: For USB CDC, baud is ignored, it runs at full speed
OUTPUT_FILE = "stm32_audio_16k.wav"
SAMPLE_RATE = 16000  # <--- Forced to 16kHz

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    ser.set_buffer_size(rx_size=1048576) # Increase PC serial buffer to 1MB
    print(f"Connected. Recording at {SAMPLE_RATE}Hz...")
except Exception as e:
    print(f"Error: {e}")
    sys.exit()

wav_file = wave.open(OUTPUT_FILE, 'wb')
wav_file.setnchannels(1)
wav_file.setsampwidth(2)
wav_file.setframerate(SAMPLE_RATE)

try:
    while True:
        if ser.in_waiting >= 1024: # Process in larger blocks to reduce CPU load
            chunk = ser.read(ser.in_waiting)
            
            # Every 4 bytes is [Byte0, Byte1, Byte2, Byte3]
            # INMP441 Data is usually in the first two bytes (0, 1) or last two (2, 3)
            # based on Left/Right justification. 
            # We slice out the 16-bit audio from the 32-bit frame:
            audio_data = bytearray()
            for i in range(0, len(chunk) // 4 * 4, 4):
                # Try frame[2:] if the audio sounds like static
                audio_data.extend(chunk[i:i+2]) 
            
            wav_file.writeframes(audio_data)
except KeyboardInterrupt:
    print("\nSaved.")
finally:
    wav_file.close()
    ser.close()
    print("Done.")