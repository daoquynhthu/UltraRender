# Fluid Dynamics Assessment

## Executive Summary

The current fluid system is not failing because of one missing tweak. It is failing because the solver, the stabilization strategy, the collision model, and the visualization pipeline are all carrying responsibilities they should not carry.

In practice, the existing implementation behaves like a heavily patched WCSPH prototype:

- it can be forced into motion
- it can sometimes be made visually plausible
- but it is not yet numerically trustworthy or architecturally stable

The correct response is not more local tuning. The correct response is to separate:

- solver correctness
- stability strategy
- scene bootstrapping
- visualization
- validation

## Non-Negotiable Principle

Complex parameter calibration must never be done manually.

That means:

- no hand-tuned mass formulas in scene code
- no scene-local stiffness rituals
- no hidden warmup-only constants that act like solver patches
- no visually chosen isolevels without a density-field definition behind them

If a parameter is important, it must be:

- derived from solver quantities
- computed from geometry or spacing
- selected by a documented preset
- or validated by an automated benchmark

## Current System Map

### Core Files

- `include/physics/fluid_system.hpp`
- `src/physics/fluid_system.cpp`
- `src/physics/physics_world.cpp`
- `include/physics/marching_cubes.hpp`
- `src/physics/marching_cubes.cpp`
- `src/main.cpp`

### Runtime Flow

The fluid path currently works like this:

1. `PhysicsWorld::step()` advances rigid bodies
2. the fluid system is sub-stepped based on solver-aware stability criteria
3. each fluid sub-step calls:
   - spatial grid build
   - density / pressure computation
   - force computation
   - explicit integration
   - particle shifting
   - boundary resolution
4. after simulation, the renderer rebuilds a triangle mesh from particles using CPU marching cubes

## Main Findings

### 1. The Solver Is Weakly Compressible and Patch-Driven

The current solver uses a weakly compressible SPH style equation of state:

- density is estimated with Poly6
- pressure is derived from a Tait-like equation
- pressure forces are computed with a custom symmetric form

This is workable for a prototype, but the implementation is not backed by a true incompressibility solve such as:

- DFSPH
- IISPH
- PBF

As a result:

- density errors are tolerated instead of solved
- stability depends on time-step reduction and clamps
- free surfaces and splashes are hard to trust

### 2. Pressure Is Artificially Clamped Into Safety

The current code uses multiple safety devices:

- force clamping
- velocity clamping
- global damping
- warmup damping
- manual stiffness ramping

These stabilize output, but they also hide the true failure mode of the solver. If the system needs all of these simultaneously, the numerical method is not yet structurally stable.

### 3. Particle Shifting Breaks the Physical Loop

`compute_particle_shift()` modifies particle positions after integration.

That is not inherently wrong, but in the current structure it is not integrated into a consistent physical correction loop. After shifting:

- density is not recomputed immediately
- neighbor relations are not rebuilt before the next dependent operation
- the shift is not framed as part of a pressure projection or density correction stage

This makes the state internally inconsistent inside a time step.

### 4. Scene Bootstrap Owns Solver Policy

`main.cpp` currently performs tasks that should belong to solver configuration and validation logic:

- smoothing radius calibration
- mass calibration from lattice assumptions
- pressure stiffness overrides
- viscosity overrides
- random/jittered particle seeding
- rigid body freezing
- warmup loops
- stiffness ramping during warmup

This creates three problems:

- solver behavior depends on scene-entry code rather than solver rules
- there is no single source of truth for fluid parameters
- debugging becomes scene-specific instead of method-specific

### 5. Collision Coupling Is Too Ad Hoc

The particle-collider coupling includes:

- a fixed particle radius
- direct positional projection
- impulse exchange with rigid bodies
- hard impulse caps

This is enough for a rough interaction demo, but not enough for reliable two-way fluid-rigid coupling.

Most importantly:

- particle radius is hard-coded instead of derived from spacing
- contact handling is not tied to the support radius or solver density model
- impulse caps can suppress physically meaningful momentum transfer

### 6. Visualization Is Not Calibrated to the Solver

Marching cubes currently rebuilds a surface every frame from `get_density_at()` and an isolevel.

The problem is not marching cubes itself. The problem is that:

- density scale depends on particle mass and smoothing radius
- those values are calibrated in multiple places
- the chosen isolevel is not derived from a stable physical meaning

So even if the solver improved, the surface may still look wrong unless the density field and isosurface threshold are normalized consistently.

### 7. There Is No Real Validation Harness

There are currently no dedicated fluid tests in `tests/`.

That means the project cannot answer basic questions automatically:

- does rest density converge?
- does a still pool remain stable?
- how much volume is lost over time?
- does a dam-break front move plausibly?
- does rigid-body coupling conserve momentum reasonably?

Without this, every solver change becomes visual guesswork.

## What Probably Caused the Earlier "Total Failure"

The earlier failures likely came from the combination of:

- explicit weakly compressible pressure response
- overly aggressive stiffness for the chosen time step
- strong non-physical clamps
- unstable initialization
- post-integration shifting
- ad hoc collision corrections

Any one of these can be survivable. Together they create a system that looks unpredictable and brittle.

## Recommended Direction

## Option A: DFSPH

Best fit if the goal is:

- physically credible liquid motion
- strong volume preservation
- better stability under larger scenes
- a more serious offline-quality foundation

Pros:

- strong incompressibility control
- better density stability than WCSPH
- good basis for coupling and rendering

Cons:

- more implementation complexity
- needs a proper iterative solver loop and validation

## Option B: PBF

Best fit if the goal is:

- robust motion quickly
- game-like stability
- easier implementation than DFSPH

Pros:

- very stable visually
- easier to get plausible behavior fast

Cons:

- less physically interpretable
- may become limiting for high-fidelity offline goals

## Option C: FLIP / APIC

Best fit if the goal is:

- large-scale liquid motion
- splash detail
- future smoke/fire and unified grid workflows

Pros:

- high-end method family
- excellent for large scenes

Cons:

- largest architectural jump
- too expensive as the immediate rescue path

## Recommendation

For this project, the best next step is:

- keep the current particle data structures and neighbor-search ideas
- stop extending the current patched WCSPH loop
- rebuild the solver core as DFSPH

That gives the best balance between:

- physical credibility
- long-term extensibility
- compatibility with the current offline renderer

## Concrete Rebuild Plan

### Phase 1: Minimal Stable Fluid Core

Keep:

- particle storage
- spatial hashing
- bounds handling

Remove or disable temporarily:

- marching cubes surface generation during debugging
- particle shifting
- surface tension
- two-way rigid-body coupling
- warmup hacks in `main.cpp`

Implement:

- rest density
- non-pressure forces
- divergence solve
- density solve
- adaptive or at least solver-aware sub-stepping

Success criteria:

- still pool remains stable
- density error stays bounded
- no explosion without force/velocity caps

### Phase 2: Validation

Add reproducible tests for:

- hydrostatic rest
- dam break
- container fill
- single-sphere interaction

Track:

- average density error
- max density error
- max speed
- particle escape count
- approximate volume drift

### Phase 3: Surface Reconstruction

Only after the solver is stable:

- normalize density field semantics
- derive an isolevel from solver quantities
- profile marching cubes cost
- consider lower-resolution preview + higher-resolution final extraction

### Phase 4: Rigid Coupling

Reintroduce:

- one-way collider interaction first
- two-way rigid-body impulses second
- only then advanced interactions like splashes in moving containers

## Short-Term Engineering Advice

Before any major solver rewrite, do these cleanup tasks:

1. move all fluid initialization policy out of `main.cpp` into a dedicated configuration layer
2. derive particle radius from spacing instead of hard-coding it
3. centralize parameter ownership
4. remove redundant safety clamps one by one and measure when instability begins
5. build the first fluid-specific tests before changing the solver

## Final Assessment

The current system is valuable as a prototype because it already contains:

- particle structures
- spatial lookup
- collision hooks
- renderer integration
- marching cubes output

But it should not be treated as a trustworthy physical base.

The right framing is:

- keep the scaffolding
- replace the solver core
- build validation before visual polish

That is the shortest path from "patched prototype" to "serious fluid subsystem."
