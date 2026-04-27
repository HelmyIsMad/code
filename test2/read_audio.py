import serial
import wave
import time

SERIAL_PORT = 'COM3'
BAUD_RATE = 115200
SAMPLE_RATE = 16000
NUM_CHANNELS = 1
SAMPLE_WIDTH = 2
DURATION_SECONDS = 5
OUTPUT_FILE = 'output.wav'
GAIN = 4.0

def apply_gain(audio_data, gain):
    import struct
    result = bytearray()
    for i in range(0, len(audio_data), 2):
        sample = struct.unpack('<h', audio_data[i:i+2])[0]
        sample = int(sample * gain)
        sample = max(-32768, min(32767, sample))
        result.extend(struct.pack('<h', sample))
    return bytes(result)

def main():
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)

    num_samples = SAMPLE_RATE * DURATION_SECONDS
    audio_data = bytearray()

    print(f"Recording {DURATION_SECONDS}s of audio...")
    while len(audio_data) < num_samples * SAMPLE_WIDTH:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            audio_data.extend(data)

    ser.close()

    audio_samples = bytes(audio_data[:num_samples * SAMPLE_WIDTH])
    if GAIN != 1.0:
        audio_samples = apply_gain(audio_samples, GAIN)

    with wave.open(OUTPUT_FILE, 'wb') as wf:
        wf.setnchannels(NUM_CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(audio_samples)

    print(f"Saved to {OUTPUT_FILE}")

if __name__ == '__main__':
    main()