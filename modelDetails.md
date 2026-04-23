For your specific constraints—**extremely limited data** (150s total) and **resource-constrained hardware** (STM32 Black Pill)—the best option is a **Quantized 1D-CNN (Convolutional Neural Network)**.

While a GMM (Gaussian Mixture Model) is traditionally better for small data, a 1D-CNN is far easier to deploy using modern TinyML tools like **TensorFlow Lite for Microcontrollers (TFLM)** or **Edge Impulse**, and it offers better noise robustness. By using heavy **Data Augmentation**, we can make the CNN think it has minutes of data instead of seconds.

Below is the `modelDetails.md` file you requested, optimized for your hardware and data constraints.

---

# Model Details: Speaker Classification for STM32

## 1. Model Selection: 1D-Convolutional Neural Network (CNN)
We have selected a **1D-CNN** over a standard Dense network or GMM. 
* **Why?** It treats the audio features (MFCCs) as a sequence, capturing temporal dependencies in speech (like the "texture" of a voice) while remaining small enough to fit in the Black Pill's RAM (<128KB).
* **Optimization:** The model will be **INT8 Quantized**. This reduces the memory footprint by 75% and allows the STM32 to use hardware-accelerated integer math instead of slow floating-point operations.



---

## 2. Feature Extraction Parameters
Raw audio cannot be fed into the model. We must convert it into a **Spectrogram-like** format using Mel-Frequency Cepstral Coefficients (MFCCs).

| Parameter | Recommended Value | Reason |
| :--- | :--- | :--- |
| **Sample Rate** | 16,000 Hz (16kHz) | Standard for human speech; saves memory compared to 44.1kHz. |
| **Frame Length** | 30 ms | Captures the "quasi-stationary" nature of speech. |
| **Frame Stride** | 20 ms | Provides a 10ms overlap to ensure no data loss between frames. |
| **MFCC Coefficients** | 13 | Captures the general shape of the vocal tract without over-complicating the model. |
| **Window Duration** | 1.0 second | The model will "listen" in 1-second chunks to make a classification. |

---

## 3. Training Strategy (Small Data Handling)
To prevent the model from overfitting on your 30s-per-speaker dataset, the following parameters must be used during training:

* **Window Slapping:** Slice the 30s clips into 1-second segments with a 50% overlap. This turns 30 seconds into **59 training samples** per speaker.
* **Data Augmentation:**
    * **Add Noise:** Inject 5–10% white noise into samples.
    * **Time Shift:** Randomly shift the audio left/right by 100ms.
* **Regularization:** Use a **Dropout Layer (0.2)** to ensure the model doesn't memorize the specific recordings.

---

## 4. Hardware Deployment Specs (STM32F411 Black Pill)
The following targets must be met during the export process (e.g., in Edge Impulse or STM32Cube.AI):

* **Inference Engine:** TensorFlow Lite for Microcontrollers (TFLM).
* **Math Library:** CMSIS-DSP (Critical for performance on ARM Cortex-M4).
* **Memory Target:** * **Flash:** < 50 KB (Weights & Code).
    * **RAM:** < 20 KB (Activation buffer).
* **Latency:** Goal of < 200ms per inference.

---

## 5. Summary Table
| Component | Choice |
| :--- | :--- |
| **Algorithm** | 1D-CNN (Int8 Quantized) |
| **Input Shape** | (49 frames, 13 MFCCs) |
| **Layers** | Conv1D -> Pool -> Conv1D -> Pool -> Dense |
| **Activation** | ReLU (Hidden), Softmax (Output) |
| **Loss Function** | Categorical Crossentropy |