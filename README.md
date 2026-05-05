# Morse Code Audio Decoder

An Arduino-based embedded system that captures audio via I2S protocol, processes it to detect Morse code tones, decodes them, and outputs text via USB keyboard emulation.

## Overview

This project implements a complete Morse code decoding pipeline:

- **Audio Capture**: 24-bit I2S audio sampling at 16 kHz
- **Signal Processing**: DC blocking filter, noise threshold detection
- **Morse Detection**: Real-time tone on/off detection with timing analysis
- **Decoding**: Morse code pattern recognition and character mapping
- **Output**: USB keyboard emulation to type decoded characters

---

## Hardware Setup

### Pin Configuration

| Function               | Pin  | Port  | Direction |
| ---------------------- | ---- | ----- | --------- |
| **SCK** (Serial Clock) | PB13 | GPIOB | Output    |
| **WS** (Word Select)   | PB12 | GPIOB | Output    |
| **SD** (Serial Data)   | PB15 | GPIOB | Input     |
| **LED**                | PC13 | GPIOC | Output    |

### I2S Protocol

- **Word Select (WS)**: Frame synchronization signal
  - `LOW` = Left channel (data transmission)
  - `HIGH` = Right channel (idle)
- **Serial Clock (SCK)**: Bit clock for synchronization
- **Serial Data (SD)**: 24-bit audio data input

---

## Signal Processing Flow

### 1. **Audio Sampling**

```c
readSample()
├─ WS_LOW()              // Start transmission
├─ Clock pulse
├─ Read 24-bit data      // MSB first
├─ Sign extend (if negative)
├─ SCK cleanup clocks
├─ WS_HIGH()             // End transmission
└─ Return 16-bit signed sample
```

**Timing**: 62 microseconds between samples → ~16 kHz sample rate

### 2. **DC Blocking Filter**

```c
dcState = raw - lastSample + (0.995 * dcState)
```

- Removes DC offset and low-frequency drift
- Alpha = 0.995 (first-order high-pass filter)
- Output: AC-coupled audio

### 3. **Magnitude Window Processing**

- Accumulates absolute magnitude over 10ms windows
- Counts samples in window (160 samples @ 16 kHz)
- Computes average magnitude: `avgMag = windowSum / windowCount`

### 4. **Tone Detection**

```
If avgMag > NOISE_THRESHOLD (5000)
  └─ Tone Active
Else
  └─ Silence
```

---

## Morse Code Decoding

### Timing Constants

| Element        | Duration    | Value    |
| -------------- | ----------- | -------- |
| **Unit**       | —           | 100 ms   |
| **Dot**        | ≤ 1.5× unit | ≤ 150 ms |
| **Dash**       | ≥ 2× unit   | ≥ 200 ms |
| **Letter Gap** | 2.5× unit   | 250 ms   |
| **Word Gap**   | 5× unit     | 500 ms   |

### Decoding State Machine

```
┌─────────────────────────────────────────────┐
│         TONE TRANSITION DETECTED             │
└─────────────────────────────────────────────┘
                       │
           ┌───────────┴───────────┐
           │                       │
      TONE ENDS              TONE STARTS
           │                       │
      Measure                 Turn LED ON
      Duration                     │
           │                       │
      Duration > 20ms?
      ├─ NO → Ignore
      ├─ YES & < 150ms → Add "." (DOT)
      └─ YES & ≥ 200ms → Add "-" (DASH)
           │
      Update currentCode
      Turn LED OFF
```

### Silence Processing

```
While in Silence:
  └─ Measure duration
      ├─ > 250ms (letter gap) → Decode & Type Character
      ├─ > 500ms (word gap) → Type SPACE
```

### Character Mapping

- **Morse Table**: 36 entries (A-Z: positions 0-25, 0-9: positions 26-35)
- **Lookup**: Match `currentCode` pattern to table entry
- **Output**: Type corresponding character via USB keyboard
- **Fallback**: Type "?" if pattern not recognized

---

## Main Loop Execution

```c
loop()
├─ Check timing (62µs interval)
│
├─ 1. Read I2S Sample
│   └─ Apply DC blocking filter
│
├─ 2. Update magnitude window
│   └─ Accumulate sample magnitude
│
└─ 3. Every 10ms window:
    ├─ Compute average magnitude
    ├─ Detect tone transition (ON→OFF or OFF→ON)
    │   ├─ Measure duration
    │   ├─ Classify as DOT or DASH
    │   └─ Update currentCode
    │
    └─ If in silence:
        ├─ Check if letter gap → Decode
        └─ Check if word gap → Type SPACE
```

---

## Configuration Parameters

### Audio Settings

```c
#define SAMPLE_RATE_HZ      16000  // Hz
#define MAG_WINDOW_MS       10     // ms
#define NOISE_THRESHOLD     5000   // ADC magnitude
#define DC_BLOCK_ALPHA      0.995f // Filter pole
```

### Morse Timing

```c
const uint32_t UNIT_MS     = 100;   // Base unit
const uint32_t DOT_MAX     = 150;   // Dot threshold
const uint32_t DASH_MIN    = 200;   // Dash threshold
const uint32_t GAP_LETTER  = 250;   // Letter separator
const uint32_t GAP_WORD    = 500;   // Word separator
```

---

## LED Indicator

- **LED ON (LOW)**: Tone detected
- **LED OFF (HIGH)**: Silence detected

---

## Supported Characters

**Letters**: A-Z (36 character map)
**Numbers**: 0-9
**Word Separator**: Space

---

## Data Flow Diagram

```
Audio Input (I2S)
    ↓
readSample() → 16-bit signed
    ↓
DC Blocking Filter → Filtered audio
    ↓
Magnitude Accumulator (10ms window)
    ↓
Tone Detection (Threshold comparison)
    ↓
Duration Timing → Dot/Dash Classification
    ↓
Morse Pattern Accumulator
    ↓
Gap Detection → Trigger Decode
    ↓
Morse Table Lookup
    ↓
USB Keyboard Output
    ↓
Character Typed
```

---

## Key Considerations

- **Sample Accuracy**: I2S protocol requires precise clock timing
- **Real-time Processing**: 62µs sample interval critical for audio fidelity
- **DC Offset Rejection**: High alpha value (0.995) preserves signal while removing drift
- **Noise Immunity**: 5000 magnitude threshold filters ambient noise
- **Timing Tolerances**: Flexible dot/dash classification handles human variation
- **Word Spacing**: Gap > 500ms enables reliable word detection
