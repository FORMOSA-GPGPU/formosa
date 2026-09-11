---
name: run-experiment
description: Experiment on Formosa with a controlled setup. Use when testing an architectural hypothesis or evaluating baseline versus variant configurations.
---

# Experiment

Experiment on Formosa to answer one architectural question. If the variant requires modifying the module hierarchy, use `compose-system` first.

## Procedure

1. **State hypothesis**
   - Formulate a single testable question defining the independent variable and expected metric change.
   - *Criterion*: Written hypothesis stating exact metric and expected direction of change.

2. **Configure baseline and single variant**
   - Identify the baseline configuration and configure exactly one isolated variant.
   - Use the runners' command-line arguments where exposed; otherwise adjust the Lua script parameters directly.
   - Keep workloads and all unrelated parameters strictly identical.
   - *Criterion*: Baseline and variant configurations differ only by the single variable under test.

3. **Select an observable simulation target**
   - Choose the smallest sufficient test command that exercises the affected architectural path and exposes the target metric.
   - Record runtime evidence that the path was active, using existing statistics or activity counters when available and a focused trace when they are not.
   - If no available workload provides this evidence, report the experiment as inconclusive. Equal measurements without path-activity evidence do not show that the change has no effect.
   - Avoid full OpenCL benchmark sweeps or multi-dimensional parameter exploration.
   - *Criterion*: Smallest sufficient test selected and its path-activity evidence recorded.

4. **Collect metrics**
   - Run baseline and variant, capturing stats via `--stats <path>` (which dumps `system.stats:dump_toml()`).
   - Use traces only when stats cannot resolve the hypothesis; inspect the runner for actual trace controls.
   - *Criterion*: Stats TOML files generated for both runs.

5. **Compare and evaluate**
   - Extract target metrics from the stats files and compute the delta.
   - Report quantitative evidence and conclude directly whether the hypothesis is supported.
   - *Criterion*: Quantitative report delivered with clear confirmation or rejection of the hypothesis.
