import numpy as np
from pathlib import Path
import librosa
import warnings
warnings.filterwarnings('ignore')

SPEAKER_NAMES = ["Abdulrahman", "Osama", "Yousri", "Karrem", "Omar"]
DATA_DIR = Path("TrainingVoiceRecords")

def load(path):
    y, sr = librosa.load(path, sr=16000, mono=True)
    return y.astype(np.float32)

def extract_deep_features(audio):
    """Extract voice characteristics independent of words"""
    features = []
    
    # 1. Pitch statistics (fundamental frequency)
    f0, voiced, probs = librosa.pyin(audio, fmin=75, fmax=500, sr=16000, frame_length=1024)
    f0_values = f0[~np.isnan(f0)]
    if len(f0_values) > 0:
        features.extend([np.mean(f0_values), np.std(f0_values), np.median(f0_values)])
        features.append(np.percentile(f0_values, 10))
        features.append(np.percentile(f0_values, 90))
    else:
        features.extend([0, 0, 0, 0, 0])
    
    # 2. Formant frequencies (vocal tract shape)
    # Approximate using spectral peaks
    for i in range(5):
        start = i * 100
        end = (i + 1) * 100
        if end > len(audio):
            segment = audio[start:]
        else:
            segment = audio[start:end]
        if len(segment) > 0:
            fft = np.abs(np.fft.rfft(segment))
            peaks = []
            for j in range(1, len(fft)-1):
                if fft[j] > fft[j-1] and fft[j] > fft[j+1] and fft[j] > np.mean(fft):
                    peaks.append(j * 16000 // len(fft))
            if peaks:
                features.append(np.mean(peaks))
            else:
                features.append(0)
        else:
            features.append(0)
    
    # 3. MFCC statistics (voice quality)
    mfcc = librosa.feature.mfcc(y=audio, sr=16000, n_mfcc=13, n_fft=512, hop_length=256)
    for i in range(13):
        features.append(np.mean(mfcc[i]))
        features.append(np.std(mfcc[i]))
    
    # 4. Energy contour statistics
    rms = librosa.feature.rms(y=audio, frame_length=1024, hop_length=256)[0]
    features.extend([np.mean(rms), np.std(rms), np.max(rms), np.min(rms)])
    
    # 5. Harmonics-to-noise ratio (voice quality)
    noise_estimate = audio - librosa.effects.harmonic(audio, margin=1.0)
    hnr = np.mean(audio**2) / (np.mean(noise_estimate**2) + 1e-10)
    features.append(hnr)
    
    return np.array(features, dtype=np.float32)

print("Extracting features...")
X, y = [], []
for i, d in enumerate(sorted(DATA_DIR.iterdir(), key=lambda x: x.name)):
    for f in d.glob("*.wav"):
        if ".org" in f.name: continue
        a = load(str(f))
        print(f"  {SPEAKER_NAMES[i]}: {len(a)/16000:.0f}s")
        for start in range(0, max(1, len(a)-16000), 8000):
            window = a[start:start+16000]
            if len(window) < 16000:
                window = np.pad(window, (0, 16000-len(window)))
            X.append(extract_deep_features(window))
            y.append(i)

X = np.array(X)
y = np.array(y)
print(f"Data: {X.shape}")

# Normalize
X_mean = X.mean(axis=0)
X_std = X.std(axis=0) + 1e-8
X = (X - X_mean) / X_std

# Train model
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import cross_val_score

clf = RandomForestClassifier(n_estimators=100, max_depth=10)
scores = cross_val_score(clf, X, y, cv=5)
print(f"CV accuracy: {scores.mean():.3f} +/- {scores.std():.3f}")

clf.fit(X, y)

print("\n--- test.wav ---")
test = load("test.wav")[:16000]
test_feat = extract_deep_features(test)
test_feat = (test_feat - X_mean) / X_std
probs = clf.predict_proba([test_feat])[0]

for i, p in enumerate(probs):
    print(f"{SPEAKER_NAMES[i]}: {p:.4f}")
    
pred = clf.predict([test_feat])[0]
print(f"Predicted: {SPEAKER_NAMES[pred]}")

# Save feature stats for embedded use
np.save("X_mean.npy", X_mean)
np.save("X_std.npy", X_std)