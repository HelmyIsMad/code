#include <Arduino.h>
#include <Keyboard.h>


#define PIN_SCK PB13
#define PIN_WS  PB12
#define PIN_SD  PB15
#define PIN_LED PC13

#define SCK_HIGH() (GPIOB->BSRR = GPIO_PIN_13)
#define SCK_LOW()  (GPIOB->BSRR = (uint32_t)GPIO_PIN_13 << 16)
#define WS_HIGH()  (GPIOB->BSRR = GPIO_PIN_12)
#define WS_LOW()   (GPIOB->BSRR = (uint32_t)GPIO_PIN_12 << 16)
#define READ_SD()  (GPIOB->IDR & GPIO_PIN_15)

#define HALF_PERIOD() do { \
  __NOP(); __NOP(); __NOP(); \
} while(0)

// --- AUDIO & FILTER SETTINGS ---
#define DC_BLOCK_ALPHA 0.995f
#define NOISE_THRESHOLD 5000   
#define SAMPLE_RATE_HZ 16000
#define SAMPLES_PER_MS (SAMPLE_RATE_HZ / 1000)
#define MAG_WINDOW_MS 10      

// --- MORSE TIMING ---
const uint32_t UNIT_MS = 100; 
const uint32_t DOT_MAX = UNIT_MS * 1.5;
const uint32_t DASH_MIN = UNIT_MS * 2;
const uint32_t GAP_LETTER = UNIT_MS * 2.5;
const uint32_t GAP_WORD = UNIT_MS * 5;

// --- VARIABLES ---
uint32_t lastMicros = 0;
const uint32_t interval = 62; 
int32_t dcState = 0;
int16_t lastSample = 0;
uint32_t windowSum = 0;
uint32_t windowCount = 0;
bool toneActive = false;
uint32_t lastTransitionTime = 0;
String currentCode = ""; 

// --- MORSE LOOKUP ---
const char* morseTable[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};
const char alphaTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// --- HELPER FUNCTIONS ---
inline void clockPulse() {
  SCK_HIGH();
  HALF_PERIOD();
  SCK_LOW();
  HALF_PERIOD();
}

int16_t readSample() {
  int32_t data = 0;
  WS_LOW();
  clockPulse(); 
  for (int i = 0; i < 24; i++) {
    SCK_HIGH();
    HALF_PERIOD();
    data = (data << 1) | (READ_SD() ? 1 : 0);
    SCK_LOW();
    HALF_PERIOD();
  }
  for (int i = 0; i < 7; i++) clockPulse();
  WS_HIGH();
  for (int i = 0; i < 32; i++) clockPulse();
  
  if (data & 0x800000) data |= 0xFF000000;
  return (int16_t)(data >> 2); 
}

void decodeAndType() {
  if (currentCode == "") return;
  for (int i = 0; i < 36; i++) {
    if (currentCode == morseTable[i]) {
      Keyboard.print(alphaTable[i]);
      currentCode = "";
      return;
    }
  }
  Keyboard.print("?"); 
  currentCode = "";
}

// --- SETUP ---
void setup() {
  delay(1000); 
  Keyboard.begin();
  
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_WS,  OUTPUT);
  pinMode(PIN_SD,  INPUT);
  pinMode(PIN_LED, OUTPUT);
  
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(PIN_WS,  HIGH);
  digitalWrite(PIN_LED, HIGH); // LED Off
}

// --- MAIN LOOP ---
void loop() {
  uint32_t nowMicros = micros();
  if (nowMicros - lastMicros < interval) return;
  lastMicros = nowMicros;

  // 1. Audio Processing
  int16_t raw = readSample();
  dcState = raw - lastSample + (int32_t)(DC_BLOCK_ALPHA * dcState);
  lastSample = raw;
  int16_t filtered = (int16_t)dcState;

  windowSum += abs(filtered);
  windowCount++;

  // 2. Magnitude Window (Every 10ms)
  if (windowCount >= (MAG_WINDOW_MS * SAMPLES_PER_MS)) {
    uint32_t avgMag = windowSum / windowCount;
    windowSum = 0;
    windowCount = 0;

    bool currentTone = (avgMag > NOISE_THRESHOLD);
    uint32_t nowMillis = millis();
    uint32_t duration = nowMillis - lastTransitionTime;

    if (currentTone != toneActive) {
      // Logic for Tone Start/End
      if (toneActive) { 
        if (duration > 20) {
          if (duration < DOT_MAX) currentCode += ".";
          else currentCode += "-";
        }
        digitalWrite(PIN_LED, HIGH); // LED Off
      } else {
        digitalWrite(PIN_LED, LOW); // LED On
      }
      toneActive = currentTone;
      lastTransitionTime = nowMillis;
    } 
    else if (!toneActive) {
      // Logic for Silence (Gaps)
      if (currentCode.length() > 0 && duration > GAP_LETTER) {
        decodeAndType();
      }
      if (duration > GAP_WORD && duration < (GAP_WORD + 50)) {
        Keyboard.write(' '); 
        lastTransitionTime = nowMillis - (GAP_WORD + 100); 
      }
    }
  }
}