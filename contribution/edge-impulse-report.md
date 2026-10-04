# Draft email / GitHub issue

Subject: ESP32-C3: reusing KISS FFT plans reduced audio inference latency by approximately 53%

Hi Edge Impulse team,

While testing a small keyword-spotting model on an ESP32-C3 at 160 MHz, I profiled the exported C++ SDK and found that repeated KISS FFT plan creation accounted for a large part of MFCC processing time.

In the generic software backend, software_rfft() allocated and initialized a plan on each call, then freed it. Our one-second audio window produces 33 frames with the same FFT length. Creating these plans took approximately 398 ms in total, while executing the FFTs took about 120 ms.

I made two local changes:

1. Reuse one KISS real-FFT plan across the MFE frames in a single invocation.
2. Reuse a separate plan across the rows of the DCT matrix operation.

Plans are owned locally and freed at the end of the corresponding operation, including error returns. The helper reconstructs a plan if its length changes. Callers without a plan retain the previous behavior. There is no persistent global cache, and the arithmetic expressions, magnitude calculation and normalization remain unchanged.

## Measured results

| Implementation | Mean full inference | Maximum full inference |
|---|---:|---:|
| Original plan handling, with detailed profiling | 781.86 ms | 785.92 ms |
| Plan reuse for MFE | 394.12 ms | 398.14 ms |
| Plan reuse for MFE and DCT, duplicate verification disabled | 365.22 ms | 369.21 ms |

The mean latency reduction is approximately 53%. Creating the MFE FFT plans dropped from approximately 398.5 ms to 12.2 ms. DCT processing dropped from approximately 64.5 ms to 35.6 ms.

## Configuration and validation

- ESP32-C3, single core, 160 MHz.
- PlatformIO Espressif32 6.13.0, Arduino ESP32 2.0.17 package, RISC-V GCC 8.4.0.
- Generic software path; ESP-DSP, ESP-NN and CMSIS optimizations disabled.
- Mono PCM16 audio, 16 kHz, one-second input windows.
- MFCC implementation version 4: 13 coefficients, 20 ms frame length, 30 ms stride, 20 Mel filters, 256-point FFT, normalization window 49, pre-emphasis 0.98.
- Quantized int8 classifier; approximately 19 ms inference time for the neural network.
- 19 fixed inputs stored in flash: six wake-word examples, six other-word examples, six ambient-noise examples and digital silence.
- 19/19 expected class matches and no runtime errors before and after the changes. These are functional regression inputs, not an independent accuracy benchmark.
- The printed output scores matched across all 19 cases at five decimal places. This does not establish bitwise equality of the complete pipeline.
- For DCT, a temporary dual-path check compared every output coefficient with memcmp and reported identical results for all recorded checks. It was disabled for the final timing measurements.
- Final free heap was 307556 bytes and historical minimum free heap was 295844 bytes; the log did not show progressive heap loss. Profiling remained enabled, with small instrumentation differences between builds.

These measurements apply to this model and software backend; other targets and FFT sizes have not been validated. Audio was read from flash, without live microphone capture or Wi-Fi.

Would you be interested in an upstream contribution implementing scoped plan reuse? I can prepare a focused patch against your preferred branch, separate from the local profiling code, with regression tests for repeated use, different lengths and allocation failure handling.

Please also confirm the preferred contribution route for the affected DSP files, since the public repository contains file-specific license notices.

Best regards,
Denis

---

Reference repository: https://github.com/edgeimpulse/inferencing-sdk-cpp
Relevant files: dsp/numpy.hpp and dsp/speechpy/feature.hpp.
This is a draft. No message, issue or pull request has been submitted.
The measurements are from a Studio-exported SDK; applicability of the complete patch to the current upstream revision still needs verification.
