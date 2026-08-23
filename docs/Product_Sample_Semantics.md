# Product sample semantics

ProductJob 0.3 has one accepted production sample domain. A positive `ProductObjective.sample_budget` selects its size. When that value is zero, a positive `NativeScene.scene.spp` is inherited; if both are unspecified, the product default is one production sample.

`NativeScene.simulation.spp_per_frame` belongs to the bounded simulation domain planned for PRV.7. It never multiplies the render sample budget. Automatic pilot work is also independent and is reported separately from completed production work.

Starting a ProductJob resets its accumulator once and advances its accepted domain through persistent bounded work quanta. ProductJob is single-use: a repeated start returns `Busy` instead of resetting or silently accumulating again. Cross-job accumulation requires a future explicit session policy and is not inferred from matching scenes or objectives.

The machine-readable precedence table is `contracts/product_sample_semantics_v0.json`. Direct and Worker tests cover scene inheritance, explicit override, simulation non-multiplication and repeated-start rejection.

The default automatic plan may contain several unbiased complete candidate techniques. `automatic_integrator.maximum_techniques` is a planning cap rather than a promised estimator set. Before allocation, ProductJob selects the largest memory-applicable candidate subset, records the resulting executor count and memory budget in its plan identity, and rejects if no complete candidate fits. This selection never changes the accepted sample domain and cannot reduce spectral lanes, precision, requested output semantics or reconstruction requirements. The retained PRV.1R quality evidence exercises this policy at 1280×720 and 1920×1080 without a capability-reduced diagnostic profile.
