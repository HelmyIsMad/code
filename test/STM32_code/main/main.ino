#include <Arduino.h>

#define PIN_SCK PB10
#define PIN_WS  PA4
#define PIN_SD  PA3

#define SCK_HIGH() (GPIOB->BSRR = GPIO_PIN_10)
#define SCK_LOW()  (GPIOB->BSRR = (uint32_t)GPIO_PIN_10 << 16)
#define WS_HIGH()  (GPIOA->BSRR = GPIO_PIN_4)
#define WS_LOW()   (GPIOA->BSRR = (uint32_t)GPIO_PIN_4 << 16)
#define READ_SD()  (GPIOA->IDR & GPIO_PIN_3)

#define HALF_PERIOD() do { \
  __NOP(); __NOP(); __NOP(); \
} while(0)

#define DC_BLOCK_ALPHA 0.995f

uint32_t lastMicros = 0;
const uint32_t interval = 71.0F;

int32_t dcState = 0;
int16_t lastSample = 0;

void setup() {
  Serial.begin(500000);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_WS,  OUTPUT);
  pinMode(PIN_SD,  INPUT);
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(PIN_WS,  HIGH);
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
  return (int16_t)dcState;
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