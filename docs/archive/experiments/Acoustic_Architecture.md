# Physics-Based Acoustic Synthesis (PBAS) Architecture

> Archive status: historical concept document. It is not an implemented-capability specification. Current physics/acoustic schema ownership is recorded in Phase Q and current runtime limits are summarized in `STATUS.md`.

## Overview
This document outlines the architecture for a "First Principles" acoustic rendering engine deeply coupled with the UltraRender physics and fluid systems. The goal is to synthesize sound physically rather than playing pre-recorded samples.

## Core Philosophy: First Principles
Sound is not an asset; it is a physical phenomenon resulting from:
1.  **Excitation**: Kinetic energy transfer (Collision, Fluid Turbulence, Fracture).
2.  **Vibration**: Structural dynamics of objects (Modal Analysis).
3.  **Propagation**: Pressure waves through a medium (Air/Water).
4.  **Perception**: Binaural reception at the camera/listener position.

## System Components

### 1. Excitation Module (Coupling Layer)
-   **Rigid Body Coupling**: Listens to `PhysicsWorld` collision events. Extracts:
    -   Impulse magnitude ($J$)
    -   Contact point ($P$)
    -   Relative velocity ($v_{rel}$)
    -   Material properties (Stiffness $k$, Damping $\zeta$)
-   **Fluid Coupling**: Listens to `FluidSystem` events.
    -   **Bubble Entrainment**: Minnaert resonance frequency $f = \frac{1}{2\pi r} \sqrt{\frac{3\gamma p_0}{\rho}}$.
    -   **Surface Impact**: Drop impact transients.
    -   **Turbulence**: Aeroacoustic noise (Lighthill analogy) for high-velocity fluids.

### 2. Synthesis Module (Vibration Layer)
-   **Modal Synthesis (Rigid Bodies)**:
    -   Each object has a **Modal Model** (pre-computed or procedural).
    -   **Modes**: Frequencies ($\omega_n$), Gains ($A_n$), and Decay rates ($\tau_n$).
    -   **Displacement**: $y(t) = \sum_{n} A_n e^{-\tau_n t} \sin(\omega_n t)$.
    -   **Excitation Mapping**: Impact location determines which modes are excited (Strike a bell at the rim vs. center).
-   **Bubble Oscillator (Fluids)**:
    -   Modeled as damped harmonic oscillators.
    -   Frequency depends on bubble radius.
    -   Amplitude depends on entrainment force.

### 3. Propagation Module (Spatial Audio)
-   **Source Directivity**: Vibration patterns emit sound differently in different directions.
-   **Doppler Effect**: Based on source and listener velocity.
-   **Reverb/Occlusion**: (Future) Ray tracing for acoustic paths (similar to light path tracing).

## Data Structures

```cpp
struct ModalMode {
    float frequency; // Hz
    float damping;   // Decay rate
    float amplitude; // Current energy
};

struct AcousticMaterial {
    float density;
    float youngs_modulus;
    float poisson_ratio;
    std::vector<ModalMode> base_modes; // Characteristic frequencies
};
```

## Integration Plan
1.  **Phase 1**: Rigid Body Impacts (Modal Synthesis).
2.  **Phase 2**: Fluid Bubble Sounds (Minnaert Resonance).
3.  **Phase 3**: Spatial Propagation.
