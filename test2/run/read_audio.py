import serial
import serial.tools.list_ports
import wave
import time
import struct
import sys
import argparse
import os
import datetime

# ── Audio settings ────────────────────────────────────────────────────────────
BAUD_RATE    = 115200
SAMPLE_RATE  = 16000
NUM_CHANNELS = 1
SAMPLE_WIDTH = 2
GAIN         = 4.0
OUTPUT_FILE  = 'output.wav'
# ─────────────────────────────────────────────────────────────────────────────


def find_stm32_port() -> str | None:
    """
    Auto-detect the STM32 USB-CDC virtual COM port.
    Looks for STMicroelectronics VID (0x0483) or common CDC descriptions.
    Returns the port name string, or None if not found.
    """
    STM32_VID = 0x0483

    for port in serial.tools.list_ports.comports():
        if port.vid == STM32_VID:
            print(f"[auto-detect] Found STM32 on {port.device}  ({port.description})")
            return port.device

    # Fallback: look for generic CDC ACM descriptors
    for port in serial.tools.list_ports.comports():
        desc = (port.description or '').lower()
        if any(k in desc for k in ('stm32', 'cdc', 'virtual com', 'serial')):
            print(f"[auto-detect] Guessing STM32 on {port.device}  ({port.description})")
            return port.device

    return None


def list_ports():
    """Print all available serial ports and exit."""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
    else:
        print("Available serial ports:")
        for p in ports:
            vid = f"VID:0x{p.vid:04X}" if p.vid else "VID:?"
            pid = f"PID:0x{p.pid:04X}" if p.pid else "PID:?"
            print(f"  {p.device:<15} {vid} {pid}  {p.description}")


def apply_gain(audio_data: bytes, gain: float) -> bytes:
    """Scale every int16 sample by `gain`, clamping to [-32768, 32767]."""
    if gain == 1.0:
        return audio_data
    result = bytearray()
    for i in range(0, len(audio_data) - 1, 2):
        sample = struct.unpack_from('<h', audio_data, i)[0]
        sample = int(sample * gain)
        sample = max(-32768, min(32767, sample))
        result.extend(struct.pack('<h', sample))
    return bytes(result)


def record(port: str, output_file: str, gain: float):
    """Open `port`, stream audio, save to `output_file` on Ctrl-C."""
    print(f"Opening {port} at {BAUD_RATE} baud …")
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
    except serial.SerialException as e:
        print(f"ERROR: Could not open port: {e}")
        sys.exit(1)

    time.sleep(2)   # let the STM32 enumerate / reset

    audio_data  = bytearray()
    start_time  = time.time()
    last_report = start_time

    print("Recording … Press Ctrl+C to stop.\n")
    try:
        while True:
            waiting = ser.in_waiting
            if waiting:
                audio_data.extend(ser.read(waiting))

            now = time.time()
            if now - last_report >= 0.5:   # update display every 0.5 s
                elapsed = now - start_time
                print(f"\r  {elapsed:6.1f}s  |  {len(audio_data):,} bytes", end='', flush=True)
                last_report = now

    except KeyboardInterrupt:
        pass
    finally:
        ser.close()

    elapsed = time.time() - start_time
    print(f"\n\nStopped. Recorded {elapsed:.1f}s, {len(audio_data):,} bytes.")

    if not audio_data:
        print("No data received — check wiring and firmware.")
        sys.exit(1)

    # Truncate to a whole number of samples
    audio_data = audio_data[: (len(audio_data) // 2) * 2]

    if gain != 1.0:
        print(f"Applying gain ×{gain} …")
        audio_data = apply_gain(bytes(audio_data), gain)

    with wave.open(output_file, 'wb') as wf:
        wf.setnchannels(NUM_CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(audio_data)

    duration = len(audio_data) / (SAMPLE_RATE * SAMPLE_WIDTH * NUM_CHANNELS)
    print(f"Saved {duration:.1f}s of audio → {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description="Record audio from an STM32 I2S-to-USB-CDC bridge."
    )
    parser.add_argument(
        '-p', '--port',
        default=None,
        help="Serial port (e.g. COM3, /dev/ttyACM0). Auto-detected if omitted."
    )
    parser.add_argument(
        '-o', '--output',
        default=OUTPUT_FILE,
        help=f"Output .wav file (default: audio/timestamp.wav)"
    )
    parser.add_argument(
        '-g', '--gain',
        type=float,
        default=GAIN,
        help=f"Linear gain multiplier (default: {GAIN})"
    )
    parser.add_argument(
        '--list-ports',
        action='store_true',
        help="List available serial ports and exit."
    )
    args = parser.parse_args()

    if args.list_ports:
        list_ports()
        return

    # Handle default output with timestamp
    if args.output == OUTPUT_FILE:
        timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
        args.output = f'audio/{timestamp}.wav'

    # Ensure the output directory exists
    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    port = args.port
    if port is None:
        port = find_stm32_port()
        if port is None:
            print("ERROR: No STM32 port found. Connect the device or use --port.")
            print()
            list_ports()
            sys.exit(1)

    record(port, args.output, args.gain)


main()