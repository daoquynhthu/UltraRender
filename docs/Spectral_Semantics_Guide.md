# Spectral Semantics Guide

## Purpose

This document defines the semantic meaning of different RGB-to-spectrum conversions used in the GPU renderer.

The core lesson from the recent volume fog and blue-shadow debugging work is simple:

- not every RGB triplet represents the same physical quantity
- therefore not every RGB triplet should use the same spectral upsampling rule

Treating surface reflectance, extinction/transmittance coefficients, and emitted radiance as if they were the same quantity creates visible bias, especially in volumetric transport and transparent shadow paths.

## The Three Semantic Classes

### 1. Surface Reflectance / Display Color

Used for:

- albedo
- F0-style colored reflectance
- artist-facing material colors

These values are interpreted as display-space color that must round-trip through the spectral path and come back to a balanced RGB appearance under the renderer's XYZ integration.

Implementation:

- `rgb_to_spectrum_value()`
- `rgb_to_spectrum()`

Location:

- `include/gpu/gpu_spectrum_utils.cuh`

Why it exists:

- this mapping is calibrated for white balance
- it intentionally compensates for the non-uniform response of the CIE curves
- it is correct for "what color should this surface look like?"

It is not correct for:

- Beer-Lambert transmittance
- scattering coefficients
- absorption coefficients
- emitted radiance spectra

### 2. Coefficients / Transport Scalars

Used for:

- transmittance `exp(-sigma_t * d)`
- scattering coefficients `sigma_s`
- absorption coefficients `sigma_a`
- neutral transmission tint in glass

Implementation:

- `rgb_coeff_to_spectrum_value()`
- `rgb_coeff_to_spectrum()`

Location:

- `include/gpu/gpu_spectrum_utils.cuh`

Why it exists:

- these quantities are not display colors
- they are wavelength-dependent coefficients that scale radiance directly
- using the calibrated surface upsampling for them introduces fake color shaping

Recent bug fixed by this rule:

- volumetric transmittance in `path_tracer_kernel.cu` was previously converted with `rgb_to_spectrum()`
- that biased transport toward blue and produced visibly cold fog and blue shadows

Affected paths that now use coefficient mapping:

- medium free-flight transmittance
- volume NEE transmittance to the light
- no-scatter survival transmittance
- transparent shadow attenuation through dielectric objects

### 3. Emission / Radiance

Used for:

- light source emission
- sun/sky radiance
- emissive materials

Implementation:

- `emission_to_spectrum()`

Current behavior:

- currently routed to `rgb_coeff_to_spectrum()`

Location:

- `include/gpu/gpu_spectrum_utils.cuh`

Why this separation still matters:

- even if emission currently reuses coefficient mapping, it has a separate semantic entry point
- that allows future replacement with a dedicated illuminant model without touching material or transport code

Future options:

- flat equal-energy emitter
- D65-like daylight emitter
- blackbody emitter
- measured SPD per light type

## Current Mapping Policy

### Use `rgb_to_spectrum()`

Use for:

- surface albedo
- metal colored reflectance terms
- artist-authored visible object color

### Use `rgb_coeff_to_spectrum()`

Use for:

- `sigma_s`
- `sigma_a`
- `sigma_t`
- Beer-Lambert transmittance
- transparent attenuation in shadow rays

### Use `emission_to_spectrum()`

Use for:

- emissive spheres
- miss/environment radiance
- sun term
- future area lights and HDR environment lights

## Recent Debugging Timeline

### Phase 1: Global Blue Volume Bias

Observed symptom:

- the whole volume fog scene looked too blue

Root cause:

- volumetric coefficients were treated as surface colors

Fix:

- introduced coefficient mapping for transmittance and medium coefficients
- neutralized sky under participating media

### Phase 2: Blue Shadows Under the Spheres

Observed symptom:

- the global fog bias was gone
- shadows under the spheres were still blue

Root cause:

- transparent shadow attenuation still converted dielectric transmission color with surface-style upsampling
- white glass therefore transmitted a spectrally blue-biased signal in the shadow extension path

Fix:

- changed transparent shadow attenuation in `extend_shadow_kernel()` to use coefficient mapping

### Phase 3: Emission Semantics

Observed symptom:

- after transport fixes, emitted light still needed semantic separation from surface reflectance

Fix:

- introduced `emission_to_spectrum()` and routed light emission and environment radiance through it

## Design Rule

Whenever an RGB value appears in the renderer, ask:

1. Is this meant to describe how a surface looks?
2. Is this a wavelength-dependent coefficient multiplying energy?
3. Is this emitted radiance?

Only after answering that question should the code choose a spectral conversion path.

If this rule is not followed, the renderer will often look "almost right" in direct light, but will fail in:

- fog
- SSS
- caustics
- colored glass
- shadowed low-energy regions

## Recommended Next Step

The current API is already better than the previous single-function design, but the next cleanup should formalize the separation further:

- `reflectance_to_spectrum()`
- `coefficient_to_spectrum()`
- `emission_to_spectrum()`

That naming is more explicit than `rgb_*` and will reduce future mistakes when adding fire, smoke, measured media, or spectral light types.
