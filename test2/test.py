import serial
import wave
import struct
import sys
import time
from serial.tools import list_ports

SAMPLE_RATE = 16000
NUM_CHANNELS = 1
SAMPLE_WIDTH = 2

def find_stm32_port():
    for port in list_ports.comports():
        if "STM32" in port.description or "STMicroelectronics" in port.description or "USB Serial" in port.description:
            return port.device
    return None

def main():
    port = find_stm32_port()
    if not port:
        print("STM32 device not found. Available ports:")
        for p in list_ports.comports():
            print(f"  {p.device}: {p.description}")
        sys.exit(1)

    print(f"Connecting to {port}...")
    ser = serial.Serial(port, 921600, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()

    print("Waiting for READY...")
    while True:
        line = ser.readline().decode('ascii', errors='replace').strip()
        if line == "READY":
            print("Recording... Press Ctrl+C to stop.")
            break
        elif line == "I2S FAIL":
            print("I2S initialization failed on STM32!")
            sys.exit(1)

    samples = []
    start_time = time.time()
    last_second = 0
    bytes_received = 0

    try:
        while True:
            if ser.in_waiting >= 2:
                data = ser.read(2)
                bytes_received += 2
                if len(data) == 2:
                    sample = struct.unpack('<h', data)[0]
                    samples.append(sample)

                    elapsed = int(time.time() - start_time)
                    if elapsed > last_second:
                        print(f"Recorded {elapsed}s ({len(samples)} samples, {bytes_received} bytes)")
                        last_second = elapsed
    except KeyboardInterrupt:
        pass

    ser.close()

    if samples:
        print(f"Saving {len(samples)} samples to recording.wav...")
        with wave.open('recording.wav', 'w') as wav_file:
            wav_file.setnchannels(NUM_CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH)
            wav_file.setframerate(SAMPLE_RATE)
            for sample in samples:
                wav_file.writeframes(struct.pack('<h', sample))
        print(f"Done! Duration: {len(samples)/SAMPLE_RATE:.2f}s")
    else:
        print("No audio samples recorded.")

if __name__ == "__main__":
    main()