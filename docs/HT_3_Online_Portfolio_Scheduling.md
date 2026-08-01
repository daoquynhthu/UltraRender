# HT.3 Online Portfolio Scheduling

Document status: Current HT.3 architecture

Last verified: 2026-08-01

HT.3 turns the eligible estimator set produced by HT.2 into a finite, replayable work schedule. It allocates technique work over explicit tile, wavelength, time and device domains, preserves independent Markov-chain accounting, detects local statistical drift, and partitions one schedule into semantically compatible distributed shards. This is an SDK-free control plane; it does not claim that every legacy CUDA technique already executes concurrently.

## Content-bound scheduling domain

`PortfolioWorkDomain` identifies the complete allocation cell:

- image extent and tile rectangle;
- spectral-domain identity and wavelength interval;
- rational physical-time interval;
- device identity;
- production sample namespace and, when needed, chain namespace.

Every domain, policy and candidate has a deterministic content identity. A candidate additionally binds its Technique Graph node, HT.2 estimate, execution semantics, output layer, scalar risk projection, measured cost and memory, sample quantum, sample/chain cursors, exploration floor and starvation policy. The schedule identity binds the Technique Graph, HT.1 composition plan, HT.2 qualification report and pilot provenance, world/snapshot identities, policy, candidate/covariance sets, epoch, budgets, predicted objective and every emitted allocation.

The scheduler accepts only candidates declared eligible by the exact qualification report. Experimental overrides require an explicit policy opt-in and remain marked experimental. Production sample ranges must be covered by the HT.2 production provenance; pilot ranges cannot be silently reused.

## Cost-aware allocation objective

For candidate `i`, the local risk model is

```text
v_i = (pilot_variance_i + tail_weight * tail_second_moment_i)
      / effective_sample_fraction_i
```

For `n_i` allocated samples and aggregation coefficient `a_i`, the projected portfolio variance is

```text
V(n) = sum_i a_i^2 v_i / n_i
     + sum_(i,j) 2 a_i a_j cov_ij min(n_i,n_j) / (n_i n_j)
```

The second term is admitted only for explicitly paired, same-domain observations with the same sample start and a positive-semidefinite covariance matrix. Ordinary sample-level covariance is rejected when either endpoint is a Markov-chain estimator: chain covariance needs independent replicate-level normalization evidence, which HT.3 does not invent from correlated samples.

Allocation starts from the mandatory floors and then chooses deterministic integer sample quanta by exact marginal variance reduction per estimated nanosecond. Stable candidate-identity tie breaking makes replay independent of input order. Persistent and scratch reservations conservatively sum all scheduled candidates; time, sample, allocation and memory overflow reject before dispatch.

This is a local pilot model, not a proof of globally optimal adaptive sampling. Projection assumptions, chain effective-sample fractions and tail penalties are content-bound planning inputs. Progressive drift detection decides when that local model has become stale.

## Exploration and starvation

Every eligible candidate receives a nonzero, quantum-aligned exploration floor. The total floor must fit both the absolute budget and the declared exploration fraction; the scheduler never drops a qualified technique merely to make an undersized budget appear feasible.

If a candidate has not been served within its epoch limit, its floor is raised to the starvation-recovery count and the resulting allocation records that recovery. This preserves evidence refresh for techniques that currently look expensive while keeping the entire decision deterministic and budgeted.

## Nonstationary worlds and re-pilot

`PortfolioDriftPolicy` compares baseline and current per-candidate observations using:

- a two-sample mean z score;
- symmetric variance ratio;
- symmetric cost ratio;
- minimum evidence and consecutive-breach requirements.

A stable world can request candidate-local re-pilot after repeated breaches. A changed world-state or observation-snapshot identity immediately invalidates the whole pilot. A configurable fraction of local breaches also escalates to global re-pilot. State and report identities bind candidate, baseline, epoch, policy and schedule, and non-monotonic updates reject.

## Distributed execution and exact coverage

A worker descriptor binds executable identity, devices and supported execution-semantics identities. `PortfolioScheduleShard` accepts only slices of the exact scheduled allocation that the worker can execute. Sample and chain ranges must remain inside the allocation; MCMC slices must preserve the declared samples-per-chain mapping.

The merge-side coverage report checks every candidate independently for exact sample and chain coverage. Missing ranges, overlap, duplicate shards, outside-allocation work, graph mismatch and incompatible worker semantics reject. The shard-set identity and coverage report are content-bound, so a nominally complete report cannot be replayed against another worker set.

## Measurement provenance

HT.3 adds an explicit `portfolio_schedule_identity` to `MeasurementProvenance`. It is compared during canonical merge and stored by MeasurementBundle checkpoint v2. The reader accepts v1 checkpoints and maps their absent schedule identity to empty legacy provenance; new writes use v2.

`make_portfolio_measurement_provenance` derives a per-shard-slice provenance record from a validated schedule and shard. It copies the exact graph, world, snapshot, time domain, sample namespace/range, schedule identity and shard producer identity. A contribution from another schedule therefore cannot merge merely because its world and sample numbers happen to match.

## Evidence

The host gate covers deterministic replay, cost/covariance-aware integer allocation, exploration and starvation recovery, MCMC chain accounting, rejection of sample-level MCMC covariance, impossible-budget rejection, repeated drift, global snapshot invalidation, exact two-worker coverage, missing/duplicate/overlapping shards and schedule-derived MeasurementBundle provenance.

The MeasurementBundle gate covers schedule-identity merge rejection, checkpoint v2 round trip and v1 compatibility. The same sources build independently with warnings as errors under `tests/sdk_free`; the installed-package consumer includes both public portfolio and measurement-binding headers.

```powershell
.\scripts\check_phase_ht3_portfolio_scheduler.ps1
ctest --test-dir build_modular_x64 -C Release -R "^(test_portfolio_scheduler|test_measurement_bundle)$" --output-on-failure
```

## Research basis

- [Grittmann et al., Efficiency-Aware Multiple Importance Sampling](https://graphics.cg.uni-saarland.de/publications/grittmann-sig2022.html) motivates optimizing statistical error together with unequal sampling cost.
- [Grittmann et al., Correlation-Aware Multiple Importance Sampling](https://graphics.cg.uni-saarland.de/publications/grittmann-2021-camis.html) motivates explicit covariance evidence rather than an independence assumption.
- [Liu et al., Change-Detection Based Framework for Piecewise-stationary Multi-Armed Bandit Problem](https://ojs.aaai.org/index.php/AAAI/article/view/11746) motivates separating online allocation from explicit nonstationarity detection and reset.
- [Cao et al., Nearly Optimal Adaptive Procedure with Change Detection for Piecewise-Stationary Bandit](https://proceedings.mlr.press/v89/cao19a.html) provides another primary reference for change-triggered adaptation under nonstationarity.

These works inform the control-plane design. They do not establish that the present greedy local objective is universally optimal for path-space transport.

## Current boundary

- HT.3 provides scheduling, provenance, drift and distributed coverage contracts. Existing CUDA kernels still do not execute all graph nodes simultaneously; lowering the automatic closed loop is HT.5.
- The scheduler allocates a content-bound scalar risk projection. Multi-observable utility learning and new proposal families belong to HT.4/HT.5 research.
- Markov-chain work retains separate chain namespaces and normalization. Cross-technique chain covariance remains unavailable until replicated chain-level evidence exists.
- World or observation identity changes force re-pilot. Finer reuse validity under physically local changes remains a research question rather than a silent heuristic.
