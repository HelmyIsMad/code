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
#define NOISE_THRESHOLD 3000
#define SAMPLE_RATE 16000
#define MAX_SAMPLES 8000

enum State { IDLE, RECORDING, CLASSIFYING };
volatile State state = IDLE;
volatile uint32_t recordingCount = 0;
uint32_t lastMicros = 0;
int16_t audioBuffer[MAX_SAMPLES];

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
  const int N_FFT = 256;
  const int HOP_LENGTH = 128;
  const int NUM_MFCC = 13;
  
  for (int i = 0; i < NUM_FEATURES; i++) features[i] = 0;
  
  int numFrames = (numSamples - N_FFT) / HOP_LENGTH + 1;
  if (numFrames <= 0) return;
  
  static float hann[N_FFT];
  static bool hannInit = false;
  if (!hannInit) {
    for (int i = 0; i < N_FFT; i++) {
      hann[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (N_FFT - 1)));
    }
    hannInit = true;
  }
  
  static float melBank[NUM_MFCC][N_FFT/2 + 1];
  static bool melInit = false;
  if (!melInit) {
    float low = 2595.0f * log10f(1.0f + 300.0f / 2595.0f);
    float high = 2595.0f * log10f(1.0f + 8000.0f / 2595.0f);
    for (int m = 0; m < NUM_MFCC; m++) {
      float f1 = 2595.0f * log10f(1.0f + 700.0f * m / 2595.0f);
      float f2 = 2595.0f * log10f(1.0f + 700.0f * (m + 1) / 2595.0f);
      for (int k = 0; k <= N_FFT/2; k++) {
        float freq = (float)k * SAMPLE_RATE / N_FFT;
        float mel = 2595.0f * log10f(1.0f + freq / 2595.0f);
        if (mel >= low && mel <= high) {
          float bank = 1.0f - fabsf(mel - (f1 + f2) * 0.5f) / ((f2 - f1) * 0.5f + 1e-10f);
          melBank[m][k] = fmaxf(0, bank);
        } else {
          melBank[m][k] = 0;
        }
      }
    }
    melInit = true;
  }
  
  float mfccMean[NUM_MFCC] = {0};
  float mfccVar[NUM_MFCC] = {0};
  
  for (int frame = 0; frame < numFrames; frame++) {
    int start = frame * HOP_LENGTH;
    
    static float frameData[N_FFT];
    for (int i = 0; i < N_FFT; i++) {
      frameData[i] = (i < numSamples - start) ? (float)audio[start + i] * hann[i] : 0;
    }
    
    static float mag[N_FFT/2 + 1];
    for (int k = 0; k <= N_FFT/2; k++) {
      float real = 0, imag = 0;
      for (int n = 0; n < N_FFT; n++) {
        float angle = -2.0f * PI * k * n / N_FFT;
        real += frameData[n] * cosf(angle);
        imag += frameData[n] * sinf(angle);
      }
      mag[k] = sqrtf(real * real + imag * imag) + 1e-10f;
    }
    
    float spec[NUM_MFCC];
    for (int m = 0; m < NUM_MFCC; m++) {
      float sum = 0;
      for (int k = 0; k <= N_FFT/2; k++) {
        sum += mag[k] * melBank[m][k];
      }
      spec[m] = logf(sum);
    }
    
    for (int m = 0; m < NUM_MFCC; m++) {
      mfccMean[m] += spec[m];
      mfccVar[m] += spec[m] * spec[m];
    }
  }
  
  for (int m = 0; m < NUM_MFCC; m++) {
    mfccMean[m] /= numFrames;
    mfccVar[m] = sqrtf(fmaxf(0, mfccVar[m] / numFrames - mfccMean[m] * mfccMean[m]));
    features[m] = mfccMean[m];
    features[m + NUM_MFCC] = mfccVar[m];
  }
}

int knnPredict(const float* features) {
  float scaled[NUM_FEATURES];
  for (int i = 0; i < NUM_FEATURES; i++) {
    scaled[i] = (features[i] - mean[i]) / scale[i];
  }
  
  int votes[NUM_SPEAKERS] = {0};
  float minDists[K_NEIGHBORS];
  int minLabels[K_NEIGHBORS];
  
  for (int i = 0; i < K_NEIGHBORS; i++) {
    minDists[i] = 1e10f;
    minLabels[i] = -1;
  }
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    float dist = 0;
    for (int j = 0; j < NUM_FEATURES; j++) {
      float diff = scaled[j] - X_train[i * NUM_FEATURES + j];
      dist += diff * diff;
    }
    dist = sqrtf(dist);
    
    for (int k = 0; k < K_NEIGHBORS; k++) {
      if (dist < minDists[k]) {
        for (int m = K_NEIGHBORS - 1; m > k; m--) {
          minDists[m] = minDists[m - 1];
          minLabels[m] = minLabels[m - 1];
        }
        minDists[k] = dist;
        minLabels[k] = y_train[i];
        break;
      }
    }
  }
  
  for (int k = 0; k < K_NEIGHBORS; k++) {
    if (minLabels[k] >= 0) votes[minLabels[k]]++;
  }
  
  int maxVotes = 0, predicted = 0;
  for (int i = 0; i < NUM_SPEAKERS; i++) {
    if (votes[i] > maxVotes) {
      maxVotes = votes[i];
      predicted = i;
    }
  }
  return predicted;
}

void loop() {
  uint32_t now = micros();
  uint32_t keyState = digitalRead(PIN_KEY);
  
  if (keyState == LOW && (millis() - lastKeyPress) > 200) {
    lastKeyPress = millis();
    
    if (state == IDLE) {
      state = RECORDING;
      recordingCount = 0;
      dcState = 0;
      lastSample = 0;
      digitalWrite(PIN_LED, LOW);
      Serial.println("Recording started...");
    } else if (state == RECORDING) {
      state = CLASSIFYING;
      digitalWrite(PIN_LED, HIGH);
      Serial.println("Recording stopped. Classifying...");
      
      float features[NUM_FEATURES];
      extractFeatures(audioBuffer, recordingCount, features);
      
      int predicted = knnPredict(features);
      Serial.print("Result: ");
      Serial.println(SPEAKER_NAMES[predicted]);
      
      state = IDLE;
    }
  }
  
  if (state == RECORDING) {
    if (now - lastMicros >= 71) {
      lastMicros += 71;
      int16_t sample = readSample();
      sample = applyFilters(sample);
      if (recordingCount < MAX_SAMPLES) {
        audioBuffer[recordingCount++] = sample;
      }
      if (recordingCount >= MAX_SAMPLES) {
        state = CLASSIFYING;
        digitalWrite(PIN_LED, HIGH);
        Serial.println("Max recording time reached. Classifying...");
        
        float features[NUM_FEATURES];
        extractFeatures(audioBuffer, recordingCount, features);
        
        int predicted = knnPredict(features);
        Serial.print("Result: ");
        Serial.println(SPEAKER_NAMES[predicted]);
        
        state = IDLE;
      }
    }
  }
}