#include <Arduino.h>

#define PIN_SCK PB13
#define PIN_WS  PB12
#define PIN_SD  PB15

#define PIN_BTN PA0
#define PIN_LED PC13

#define SCK_HIGH() (GPIOB->BSRR = GPIO_PIN_13)
#define SCK_LOW()  (GPIOB->BSRR = (uint32_t)GPIO_PIN_13 << 16)
#define WS_HIGH()  (GPIOB->BSRR = GPIO_PIN_12)
#define WS_LOW()   (GPIOB->BSRR = (uint32_t)GPIO_PIN_12 << 16)
#define READ_SD()  (GPIOB->IDR & GPIO_PIN_15)

#define HALF_PERIOD() do { \
  __NOP(); __NOP(); __NOP(); \
} while(0)

#define DC_BLOCK_ALPHA 0.995f
#define NOISE_THRESHOLD 300

uint32_t lastMicros = 0;
const uint32_t interval = 62.5F;

int32_t dcState = 0;
int16_t lastSample = 0;

void setup() {
  Serial.begin(500000);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_WS,  OUTPUT);
  pinMode(PIN_SD,  INPUT);
  pinMode(PIN_BTN, INPUT_PULLDOWN);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(PIN_WS,  HIGH);
  digitalWrite(PIN_LED, HIGH);
}

inline void clockPulse() {
  SCK_HIGH();
  HALF_PERIOD();
  SCK_LOW();
  HALF_PERIOD();
}

int16_t readSample() {
  int32_t data = 0;

  // WS low = Left channel. MSB follows one BCLK later (I2S standard delay).
  WS_LOW();
  clockPulse(); // Consume the 1-cycle I2S delay — SD not sampled here

  // Read 24 bits MSB-first, sampling on rising edge
  for (int i = 0; i < 24; i++) {
    SCK_HIGH();
    HALF_PERIOD();         // Let SD settle before reading
    data = (data << 1) | (READ_SD() ? 1 : 0);
    SCK_LOW();
    HALF_PERIOD();
  }

  // Clock out remaining 7 bits of the 32-bit left slot (bits 24–31, unused)
  for (int i = 0; i < 7; i++) clockPulse();

  // WS high = Right channel
  WS_HIGH();

  // Clock out all 32 bits of right channel (discard)
  for (int i = 0; i < 32; i++) clockPulse();

  // Sign-extend 24-bit two's complement to 32-bit
  if (data & 0x800000) data |= 0xFF000000;

  // Scale to 16-bit. Use >> 8 for full range, >> 2 for max gain.
  return (int16_t)(data >> 2);
}

int16_t applyFilters(int16_t sample) {
  // DC blocker (removes DC offset and low rumble)
  dcState = sample - lastSample + (int32_t)(DC_BLOCK_ALPHA * dcState);
  lastSample = sample;
  sample = (int16_t)dcState;
  
  // Simple noise gate - mute samples below threshold
  if (abs(sample) < NOISE_THRESHOLD) {
    return 0;
  }
  return sample;
}

void loop() {
  uint32_t now = micros();
  if (now - lastMicros < interval) return;
  lastMicros += interval;

  int16_t sample = readSample();
  sample = applyFilters(sample);
  
  if (Serial.availableForWrite() >= 2) {
    Serial.write((uint8_t*)&sample, 2);
  }
}