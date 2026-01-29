# Handover Document: Acoustic Engine & Physics Integration

## 1. Current Status
- **Build Status**: Failed (Compilation Errors)
- **Last Action**: Refactoring `AcousticSystem` to use `std::shared_ptr` and fixing Physics linkage.
- **Pending Tasks**: Fix `main.cpp` syntax errors, Verify `acoustic_test.scene`.

## 2. Known Errors & Fixes

### A. `src/main.cpp` - Shared Pointer Usage
**Issue**: `acoustic_system` was converted to `std::shared_ptr`, but member access still uses `.` instead of `->`.
**Affected Lines**:
- Line 434, 436, 439: `acoustic_system.register_body(...)` -> `acoustic_system->register_body(...)`
- Line 887: `acoustic_system.set_listener(...)` -> `acoustic_system->set_listener(...)`
- Line 891: `acoustic_system.set_physics_world(...)` -> `acoustic_system->set_physics_world(...)`
- Line 964: `acoustic_system.generate_samples(...)` -> `acoustic_system->generate_samples(...)`

### B. `src/main.cpp` - Pointer Registration
**Issue**: Incorrect argument type passed to `register_listener`.
**Location**: Line 430
**Fix**: Change `physics_world->register_listener(&acoustic_system);` to `physics_world->register_listener(acoustic_system.get());`

### C. `src/main.cpp` - Brace Mismatch (C1075)
**Issue**: Missing closing brace `}` likely in the `try/catch` block inside the physics loop.
**Investigation**: Check lines 947-1106. Ensure `try`, `for`, and `if` blocks are properly closed. The `try` block starting at line 947 needs a corresponding `catch` block which seems missing or misplaced.

## 3. Configuration & Assets
- **Scene File**: Created `scenes/acoustic_test.scene` with Glass and Metal spheres for acoustic testing.
- **Materials**: IDs 1 (Metal), 2 (Wood), 3 (Glass) are hardcoded in `main.cpp` registration.

## 4. Next Steps
1. Apply fixes to `src/main.cpp`.
2. Run `build_gpu.bat`.
3. Run `run_simulation.bat` with `acoustic_test.scene`.
4. Verify audio output for collision sounds and occlusion.
