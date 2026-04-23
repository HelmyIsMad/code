/* prod/main/main.ino
   Speaker classification using template matching
   Button: PA0, LED: PC13 (active low)
*/

#include <Arduino.h>
#include "model.h"

const uint8_t BUTTON_PIN = PA0;
const uint8_t LED_PIN = PC13;
const uint8_t MIC_PIN = PA3;

const unsigned long DEBOUNCE_MS = 50;
const unsigned long MAX_RECORD_MS = 10000UL;

const int SAMPLE_RATE = 16000;
const int MAX_SAMPLES = 16000;
const int MFCC_FRAMES = 49;
const int MFCC_BINS = 13;
const int TEMPLATE_SIZE = MFCC_FRAMES * MFCC_BINS;
const int NUM_SPEAKERS = 5;

bool isRecording = false;
bool hasAudioData = false;
unsigned long recordingStartedMs = 0;
uint8_t buttonState = HIGH;
uint8_t lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;

volatile int audioSampleCount = 0;
int16_t audioBuffer[MAX_SAMPLES];

const char* SPEAKER_NAMES[NUM_SPEAKERS] = {"Abdulrahman", "Osama", "Yousri", "Karrem", "Omar"};

float templates[NUM_SPEAKERS][TEMPLATE_SIZE];

void load_templates() {
    const unsigned char* data = g_model;
    for (int i = 0; i < NUM_SPEAKERS; i++) {
        for (int j = 0; j < TEMPLATE_SIZE; j++) {
            float* f = (float*)(data + i * TEMPLATE_SIZE * sizeof(float) + j * sizeof(float));
            templates[i][j] = *f;
        }
    }
    Serial.println("Templates loaded");
}

void compute_mfcc_simple(float* audio, int len, float* mfcc_out) {
    const int n_fft = 512;
    const int hop = 320;
    const int num_bins = 25;
    const int n_mfcc = MFCC_BINS;
    
    float mel_lo[26];
    for (int i = 0; i <= num_bins; i++) {
        mel_lo[i] = 2595.0f * log10f(1.0f + (float(i) / num_bins) * 5000.0f / 2595.0f);
    }
    
    for (int frame = 0; frame < MFCC_FRAMES; frame++) {
        int start = frame * hop / 2;
        if (start + n_fft > len) break;
        
        float windowed[512];
        for (int i = 0; i < n_fft && start + i < len; i++) {
            float w = 0.5f * (1.0f - cosf(3.14159265f * i / (n_fft - 1)));
            windowed[i] = audio[start + i] * w;
        }
        
        float energy[25];
        for (int b = 0; b < num_bins; b++) {
            int bs = (int)((float)(n_fft + 1) * mel_lo[b] / SAMPLE_RATE);
            int be = (int)((float)(n_fft + 1) * mel_lo[b + 1] / SAMPLE_RATE);
            float e = 0.0f;
            for (int k = max(0, bs); k < min(n_fft/2, be); k++) {
                e += windowed[k] * windowed[k];
            }
            energy[b] = logf(e + 1e-10f);
        }
        
        for (int i = 0; i < n_mfcc; i++) {
            float sum = 0.0f;
            for (int j = 0; j < num_bins; j++) {
                sum += energy[j] * cosf(3.14159265f * i * (j + 0.5f) / num_bins);
            }
            mfcc_out[frame * n_mfcc + i] = sum * 0.01f;
        }
    }
    
    for (int i = 0; i < MFCC_FRAMES * n_mfcc; i++) {
        mfcc_out[i] = fmaxf(-5.0f, fminf(5.0f, mfcc_out[i]));
    }
}

float normalize(float* v, int size) {
    float sum = 0.0f;
    for (int i = 0; i < size; i++) sum += v[i] * v[i];
    return sqrtf(sum + 1e-8f);
}

int predict_speaker(float* mfcc) {
    float norm = normalize(mfcc, TEMPLATE_SIZE);
    for (int i = 0; i < TEMPLATE_SIZE; i++) mfcc[i] /= norm;
    
    float sim[NUM_SPEAKERS];
    for (int s = 0; s < NUM_SPEAKERS; s++) {
        float t_norm = normalize(templates[s], TEMPLATE_SIZE);
        float dot = 0.0f;
        for (int i = 0; i < TEMPLATE_SIZE; i++) {
            dot += mfcc[i] * (templates[s][i] / t_norm);
        }
        sim[s] = dot;
        Serial.print(SPEAKER_NAMES[s]);
        Serial.print(": ");
        Serial.println(dot);
    }
    
    int best = 0;
    for (int i = 1; i < NUM_SPEAKERS; i++) {
        if (sim[i] > sim[best]) best = i;
    }
    return best;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(MIC_PIN, INPUT);
  digitalWrite(LED_PIN, HIGH);
  
  load_templates();
  
  Serial.println("Speaker Classifier Ready");
  Serial.println("Press button (PA0) to record");
}

void loop() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        if (!isRecording) {
          isRecording = true;
          recordingStartedMs = millis();
          audioSampleCount = 0;
          digitalWrite(LED_PIN, LOW);
          Serial.println("RECORDING: started");
        } else {
          isRecording = false;
          digitalWrite(LED_PIN, HIGH);
          Serial.println("RECORDING: stopped");
          hasAudioData = true;
        }
      }
    }
  }
  lastButtonReading = reading;
  
  if (isRecording) {
    if (audioSampleCount < MAX_SAMPLES) {
      audioBuffer[audioSampleCount] = (int16_t)((analogRead(MIC_PIN) - 2048) << 4);
      audioSampleCount++;
    }
    if ((millis() - recordingStartedMs) >= MAX_RECORD_MS) {
      isRecording = false;
      digitalWrite(LED_PIN, HIGH);
      Serial.println("RECORDING: max duration");
      hasAudioData = true;
    }
  }
  
  if (hasAudioData && audioSampleCount > 512) {
    hasAudioData = false;
    
    Serial.println("Processing...");
    
    float audio[MAX_SAMPLES];
    float mean = 0;
    for (int i = 0; i < audioSampleCount; i++) {
        audio[i] = (float)audioBuffer[i] / 32768.0f;
        mean += audio[i];
    }
    mean /= audioSampleCount;
    for (int i = 0; i < audioSampleCount; i++) audio[i] -= mean;
    
    float mfcc[TEMPLATE_SIZE];
    compute_mfcc_simple(audio, audioSampleCount, mfcc);
    
    int result = predict_speaker(mfcc);
    
    Serial.print("RESULT: ");
    Serial.println(SPEAKER_NAMES[result]);
    
    audioSampleCount = 0;
  }
  
  delay(1);
}