# Speaker Identification System - Connection Guide

## Hardware Components

### STM32F401RCT6 (Pin Configuration)
- **Package**: LQFP64
- **Flash**: 256 KB
- **RAM**: 64 KB
- **CPU**: ARM Cortex-M4 @ 84 MHz

### INMP441 Microphone (I2S Digital MEMS)
| Pin | Function | STM32 Pin |
|-----|----------|----------|
| VDD | Power (3.3V) | 3.3V |
| GND | Ground | GND |
| SCK | I2S Clock | PB13 |
| WS | Word Select | PB12 |
| SD | Serial Data | PB15 |
| L/R | Left/Right Channel | GND (tie to GND for Left channel) |

### Controls
| Component | STM32 Pin | Description |
|-----------|----------|------------|
| Button | PA0 | Start/Stop Recording (Active High, Pull-down) |
| LED | PC13 | Recording Indicator (Active Low = ON) |

## I2S Configuration (STM32duino I2S Library)
- **Mode**: Master
- **Data Format**: I2S Phillips
- **Data Size**: 16-bit
- **Sample Rate**: 16 kHz

## Pin Mapping Summary (STM32duino I2S)
| INMP441 | STM32F401RCT6 | Arduino Pin |
|---------|---------------|-------------|
| SCK | PB13 | (setSCK) |
| WS | PB12 | (setWS) |
| SD | PB15 | (setSD) |
| VDD | 3.3V | 3.3V |
| GND | GND | GND |
| L/R | GND | GND |
| Button | PA0 | A0 |
| LED | PC13 | PC13 |