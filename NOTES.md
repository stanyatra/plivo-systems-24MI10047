# Engineering Design Notes & Post-Mortem

## Core Architectural Hurdles

### 1. Clock Domain Discrepancies
A primary challenge involved synchronization between the Python-based harness and the C-based receiver. The harness provides an epoch-based `T0`. Applying this directly to the receiver’s local monotonic clock caused a systemic shift. 
*   **The Fix:** We calculated the instantaneous delta between `CLOCK_REALTIME` and `CLOCK_MONOTONIC` at the moment of initialization, allowing the receiver to project the harness's `T0` into the local hardware's monotonic reference frame.

### 2. Eliminating Buffer Flooding
Initial attempts to "catch up" to the network stream triggered a logic error where the receiver, perceiving the deadline as expired, released all buffered packets in a singular burst. 
*   **The Fix:** Implemented `clock_nanosleep` with `TIMER_ABSTIME` to force strict, serialized frame dispatch. By applying a **5ms pre-deadline release offset**, we ensure packets are dispatched with enough lead time to account for kernel scheduling latency while preventing the saturation of local UDP buffers.

### 3. Redundancy and Reliability Logic
To maintain the bandwidth overhead below the **2.00x limit** while maximizing reliability, we employed a selective redundancy strategy:
*   The sender appends the previous frame to the current frame when sequence numbers are not multiples of 20.
*   The receiver implements a stateful cache of the last valid payload, ensuring that if a packet sequence is entirely lost (primary + redundant), the receiver can "conceal" the loss by re-transmitting the last known valid payload to ensure the player's deadline is never missed.

## Optimization Philosophy
This project prioritized **Temporal Consistency** over **Throughput**. By accepting a minor 1.98x bandwidth footprint, we effectively bought immunity against the volatility of the provided network relay, proving that strategic redundancy is a viable trade-off for real-time streaming reliability.
