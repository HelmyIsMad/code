import numpy as np
from pathlib import Path
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers
import librosa
import warnings
warnings.filterwarnings('ignore')

SAMPLE_RATE = 16000
MFCC_BINS = 20
MFCC_FRAMES = 20
SPEAKER_NAMES = ["Abdulrahman", "Osama", "Yousri", "Karrem", "Omar"]
WINDOW = 16000
STRIDE = 8000

DATA_DIR = Path("TrainingVoiceRecords")

def load(path):
    y, sr = librosa.load(path, sr=SAMPLE_RATE, mono=True)
    return y.astype(np.float32)

def extract(audio):
    mfcc = librosa.feature.mfcc(y=audio, sr=SAMPLE_RATE, n_mfcc=MFCC_BINS, n_fft=512, hop_length=512)
    if mfcc.shape[1] < MFCC_FRAMES:
        mfcc = np.pad(mfcc, ((0,0),(0,MFCC_FRAMES-mfcc.shape[1])))
    return mfcc[:, :MFCC_FRAMES].T

X, y = [], []
for i, d in enumerate(sorted(DATA_DIR.iterdir(), key=lambda x: x.name)):
    for f in d.glob("*.wav"):
        if ".org" in f.name: continue
        a = load(str(f))
        dur = len(a)/SAMPLE_RATE
        for start in range(0, len(a)-WINDOW, STRIDE):
            aa = a[start:start+WINDOW]
            for _ in range(5):
                aug = aa.copy()
                if _ >= 1: aug = aug + np.random.randn(WINDOW) * 0.005
                if _ >= 2: aug = np.roll(aug, np.random.randint(-1600, 1600))
                if _ >= 3: aug = aug * np.random.uniform(0.9, 1.1)
                if _ >= 4: aug = librosa.effects.pitch_shift(aug, sr=SAMPLE_RATE, n_steps=np.random.uniform(-1, 1))
                X.append(extract(aug))
                y.append(i)
        print(f"{SPEAKER_NAMES[i]}: {dur:.0f}s -> {len(X)} samples")

X = np.array(X)
y = np.array(y)
X = (X - X.mean()) / (X.std() + 1e-8)
print(f"\nData: {X.shape}")

model = keras.Sequential([
    layers.Input(shape=(MFCC_FRAMES, MFCC_BINS)),
    layers.Conv1D(32, 3, activation='relu', padding='same'),
    layers.MaxPooling1D(2),
    layers.Conv1D(64, 3, activation='relu', padding='same'),
    layers.GlobalAveragePooling1D(),
    layers.Dropout(0.3),
    layers.Dense(5, activation='softmax')
])

model.compile(optimizer='adam', loss='sparse_categorical_crossentropy', metrics=['accuracy'])
model.fit(X, y, epochs=30, batch_size=64, verbose=2)

print("\n--- test.wav ---")
test = load("test.wav")[:WINDOW]
test_mfcc = extract(test)
test_mfcc = (test_mfcc - X.mean()) / (X.std() + 1e-8)
pred = model.predict(test_mfcc.reshape(1, MFCC_FRAMES, MFCC_BINS), verbose=0)
print("Probs:", pred[0])
print("Pred:", SPEAKER_NAMES[np.argmax(pred[0])])

converter = tf.lite.TFLiteConverter.from_keras_model(model)
tflite_model = converter.convert()

header = """// Speaker model
#ifndef MODEL_H_
#define MODEL_H_
#include <cstddef>
extern const unsigned char g_model[];
extern const int g_model_len;
#endif
"""
with open("prod/main/model.h", 'w', encoding='utf-8') as f:
    f.write(header)
    f.write("const unsigned char g_model[] = {\n")
    for i, b in enumerate(tflite_model):
        f.write(f"0x{b:02x}")
        if i < len(tflite_model)-1: f.write(",")
        if (i+1)%16==0: f.write("\n")
    f.write("\n};\n")
    f.write(f"const int g_model_len = {len(tflite_model)};\n")
print(f"Saved: {len(tflite_model)} bytes")