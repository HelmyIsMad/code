#include <Arduino.h>
#include "model.h"

#define PIN_SCK PB10
#define PIN_WS  PA4
#define PIN_SD  PA3
#define PIN_KEY PA0
#define PIN_LED PC13

#define SCK_HIGH() (GPIOB->BSRR = GPIO_PIN_10)
#define SCK_LOW()  (GPIOB->BSRR = (uint32_t)GPIO_PIN_10 << 16)
#define WS_HIGH()  (GPIOA->BSRR = GPIO_PIN_4)
#define WS_LOW()   (GPIOA->BSRR = (uint32_t)GPIO_PIN_4 << 16)
#define READ_SD()  (GPIOA->IDR & GPIO_PIN_3)

#define HALF_PERIOD() do { __NOP(); __NOP(); __NOP(); } while(0)

#define DC_BLOCK_ALPHA 0.995f
#define NOISE_THRESHOLD 30
#define SAMPLE_RATE 16000
#define MAX_SAMPLES 16000

enum State { IDLE, RECORDING, CLASSIFYING };
volatile State state = IDLE;
volatile uint32_t recordingCount = 0;
volatile uint32_t lastMicros = 0;
int16_t* audioBuffer = nullptr;

int32_t dcState = 0;
int16_t lastSample = 0;
uint32_t lastKeyPress = 0;

void setup() {
  Serial.begin(500000);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_WS, OUTPUT);
  pinMode(PIN_SD, INPUT);
  pinMode(PIN_KEY, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(PIN_WS, HIGH);
  digitalWrite(PIN_LED, HIGH);
  
  Serial.print("NUM_FEATURES=");
  Serial.println(NUM_FEATURES);
  Serial.print("NUM_SAMPLES=");
  Serial.println(NUM_SAMPLES);
  Serial.print("TEMPLATE_SIZE=");
  Serial.println(TEMPLATE_SIZE);
  Serial.println("Ready");
}

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

int16_t applyFilters(int16_t sample) {
  dcState = sample - lastSample + (int32_t)(DC_BLOCK_ALPHA * dcState);
  lastSample = sample;
  sample = (int16_t)dcState;
  if (abs(sample) < NOISE_THRESHOLD) return 0;
  return sample;
}

void extractFeatures(const int16_t* audio, int numSamples, float* features) {
  for (int i = 0; i < NUM_FEATURES; i++) features[i] = 0;
  if (numSamples < 1000) {
    Serial.println("Too few samples");
    return;
  }
  
  float maxVal = 0.001f;
  for (int i = 0; i < numSamples; i++) {
    float v = fabsf((float)audio[i]);
    if (v > maxVal) maxVal = v;
  }
  Serial.print("maxVal=");
  Serial.println(maxVal);
  
  float sum = 0, sumSq = 0;
  for (int i = 0; i < numSamples; i++) {
    float v = (float)audio[i] / maxVal;
    sum += fabsf(v);
    sumSq += v * v;
  }
  
  int idx = 0;
  int seg16 = numSamples / 16;
  for (int i = 0; i < 16; i++) {
    float segSum = 0;
    for (int j = i*seg16; j < (i+1)*seg16; j++) segSum += fabsf((float)audio[j] / maxVal);
    features[idx++] = segSum / seg16;
  }
  
  for (int i = 0; i < 16; i++) {
    float segSum = 0;
    for (int j = i*seg16; j < (i+1)*seg16; j++) {
      float v = (float)audio[j] / maxVal;
      segSum += v * v;
    }
    features[idx++] = sqrtf(segSum / seg16);
  }
  
  features[idx++] = sum / numSamples;
  features[idx++] = sqrtf(fmaxf(0, sumSq/numSamples - (sum/numSamples)*(sum/numSamples)));
  features[idx++] = sumSq / numSamples;
  features[idx++] = sumSq / numSamples;
  
  int seg8 = numSamples / 8;
  for (int i = 0; i < 8; i++) {
    int zc = 0;
    float prev = (float)audio[i*seg8] / maxVal;
    for (int j = i*seg8 + 1; j < (i+1)*seg8; j++) {
      float curr = (float)audio[j] / maxVal;
      if ((prev > 0 && curr <= 0) || (prev < 0 && curr >= 0)) zc++;
      prev = curr;
    }
    features[idx++] = (float)zc / seg8;
  }
  
  int lags[] = {20, 40, 60, 80, 100, 150, 200, 250, 300, 400};
  for (int l = 0; l < 10; l++) {
    int lag = lags[l];
    float ac = 0, ac0 = 0;
    for (int i = 0; i < numSamples - lag; i += 10) {
      float a = (float)audio[i] / maxVal;
      float b = (float)audio[i + lag] / maxVal;
      ac += a * b;
      ac0 += a * a;
    }
    features[idx++] = (ac0 > 0) ? (ac / ac0) : 0;
  }
  
  for (int b = 0; b < 16; b++) {
    int start = b * numSamples / 16;
    int end = (b+1) * numSamples / 16;
    float bandSum = 0;
    for (int i = start; i < end; i++) {
      float v = (float)audio[i] / maxVal;
      bandSum += v * v;
    }
    features[idx++] = bandSum / (end - start);
  }
  
  Serial.print("Features extracted: ");
  Serial.println(idx);
}

int nearestCentroid(const float* features) {
  float scaled[70];
  for (int i = 0; i < NUM_FEATURES; i++) {
    scaled[i] = (features[i] - mean[i]) / (scale[i] + 1e-6f);
  }
  
  float minDist = 1e10f;
  int predicted = -1;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    float dist = 0;
    const float* cent = &centroids[i * NUM_FEATURES];
    for (int j = 0; j < NUM_FEATURES; j++) {
      float diff = scaled[j] - cent[j];
      dist += diff * diff;
    }
    if (dist < minDist) {
      minDist = dist;
      predicted = i;
    }
  }
  Serial.print("Predicted index: ");
  Serial.print(predicted);
  Serial.print(" dist: ");
  Serial.println(minDist);
  return predicted;
}

void loop() {
  uint32_t now = micros();
  uint32_t keyState = digitalRead(PIN_KEY);
  
  if (keyState == LOW && (millis() - lastKeyPress) > 200) {
    lastKeyPress = millis();
    
    if (state == IDLE) {
      if (audioBuffer == nullptr) {
        audioBuffer = (int16_t*)malloc(MAX_SAMPLES * sizeof(int16_t));
      }
      memset(audioBuffer, 0, MAX_SAMPLES * sizeof(int16_t));
      state = RECORDING;
      recordingCount = 0;
      dcState = 0;
      lastSample = 0;
      lastMicros = now;
      digitalWrite(PIN_LED, LOW);
      Serial.println("Recording started...");
    } else if (state == RECORDING) {
      state = CLASSIFYING;
      digitalWrite(PIN_LED, HIGH);
      Serial.print("Stopped, ");
      Serial.print(recordingCount);
      Serial.println(" samples");
      
      float feat[70];
      extractFeatures(audioBuffer, recordingCount, feat);
      int pred = nearestCentroid(feat);
      
      if (pred >= 0 && pred < NUM_SAMPLES) {
        Serial.print("Result: ");
        Serial.println(SPEAKER_NAMES[pred]);
      } else {
        Serial.println("Result: UNKNOWN");
      }
      
      state = IDLE;
      recordingCount = 0;
    }
  }
  
  if (state == RECORDING && audioBuffer != nullptr) {
    if ((int32_t)(now - lastMicros) >= 62) {
      lastMicros += 62;
      
      int16_t sample = readSample();
      sample = applyFilters(sample);
      
      if (recordingCount < MAX_SAMPLES) {
        audioBuffer[recordingCount++] = sample;
      }
      
      if (now % 1000000 == 0) {
        Serial.print("Recording: ");
        Serial.println(recordingCount);
      }
    }
  }
  
  if (state == RECORDING && recordingCount >= MAX_SAMPLES) {
    state = CLASSIFYING;
    digitalWrite(PIN_LED, HIGH);
    Serial.println("Buffer full");
    
    float feat[70];
    extractFeatures(audioBuffer, recordingCount, feat);
    int pred = nearestCentroid(feat);
    
    if (pred >= 0 && pred < NUM_SAMPLES) {
      Serial.print("Result: ");
      Serial.println(SPEAKER_NAMES[pred]);
    } else {
      Serial.println("Result: UNKNOWN");
    }
    
    state = IDLE;
    recordingCount = 0;
  }
}