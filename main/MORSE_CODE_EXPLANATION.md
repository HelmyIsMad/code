# Morse Code Decoder - Complete Explanation

This Arduino sketch reads Morse code from an I2S microphone and decodes it to type characters on a keyboard. Here's how it works:

## **1. Hardware Setup**

```
GPIO Pins:
- PB13 (SCK)  → I2S Clock signal
- PB12 (WS)   → I2S Word Select (left/right channel)
- PB15 (SD)   → I2S Serial Data (audio input)
- PC13 (LED)  → Status indicator
```

The macros (`SCK_HIGH()`, `SCK_LOW()`, etc.) directly manipulate GPIOB registers for fast GPIO control instead of using `digitalWrite()`.

## **2. Audio Capture - `readSample()`**

This reads a 24-bit I2S audio sample:
1. **WS_LOW()** - Start data frame
2. **Clock in 24 bits** - One bit per clock pulse from the SD pin
3. **Left-shift bits** - Build the 24-bit number
4. **WS_HIGH()** - End frame
5. **Wait 32 clock cycles** - I2S protocol requirement
6. **Convert to int16** - Shift from 24-bit to 16-bit audio

**Result**: A single audio sample (-32768 to +32767) every 62 microseconds.

## **3. Audio Processing (DC Blocking)**

```
dcState = raw - lastSample + (0.995 * dcState)
```

This **removes DC offset** (low-frequency noise) using a high-pass filter. The 0.995 coefficient keeps mostly the previous filtered value, removing slow drift while preserving fast audio changes.

## **4. Tone Detection - The Core Logic**

Every **10ms**, the code:
1. **Calculates average magnitude** - Sum of absolute values of 160 samples
2. **Compares to threshold** (5000) - If `avgMag > 5000`, a tone is present
3. **Detects transitions** - When tone starts or stops

```
Tone Active → Silent:
  If duration < 150ms  → DOT (.)
  If duration > 200ms  → DASH (-)

Silent → Tone Active:
  Track gap duration for letter/word spacing
```

## **5. Morse Code Timing**

| Event | Duration |
|-------|----------|
| DOT | < 150ms |
| DASH | > 200ms |
| Gap between dots/dashes in a letter | 250ms |
| Gap between letters | 250-500ms |
| Gap between words | 500ms+ |

## **6. Decoding - `decodeAndType()`**

When a letter gap is detected:
1. **Search morse table** - Find the dot/dash pattern (e.g., ".-" = A)
2. **Type the character** via `Keyboard.print()`
3. **Spaces between words** - Auto-inserted when gap > 500ms

## **7. Main Loop Flow**

```
┌─ Sample audio every 62μs
│
├─ Apply DC filter to remove noise
│
├─ Accumulate magnitude for 10ms window
│
├─ Every 10ms: Check if tone is present
│  ├─ Tone START → Turn LED on
│  ├─ Tone STOP → Measure duration → Decode as DOT/DASH
│  └─ Silence → Check for letter/word gaps → Type character/space
│
└─ Repeat
```

## **Example: Detecting "A" (dot-dash)**

1. Operator sends **dot** (100ms tone) → Code adds `.`
2. Operator sends **dash** (200ms tone) → Code adds `-`
3. 250ms silence → Code detects `currentCode = ".-"` → Types `A`

## **Key Variables**

| Variable | Purpose |
|----------|---------|
| `toneActive` | Is audio currently above threshold? |
| `currentCode` | Accumulates dots/dashes for current letter |
| `windowSum` | Accumulates audio magnitude for 10ms window |
| `lastTransitionTime` | When did tone start/stop? |

---

## **TL;DR**

Microphone → ADC reads audio → DC filter removes noise → Measures if signal is loud (tone) → Times duration of tones → Converts timing to dots/dashes → Looks up dots/dashes in Morse table → Types matching character.

