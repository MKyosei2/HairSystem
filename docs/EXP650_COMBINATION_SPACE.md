# EXP650 combination space

The 650-case runtime suite treats the experimental options as:

1. `enableMicrotype`
2. `enableCBMA`
3. `enablePhysicsIslands`
4. `enableRodRepair`
5. `enableGpuResident`
6. `enableNoNoveltyBaseline`

`enableNoNoveltyBaseline` is the neutral current-project baseline and is mutually exclusive with the five novelty flags.
It is not combined with any novelty flag.

The default suite therefore contains:

- `Combo_00000` through `Combo_11111`: all `2^5 = 32` combinations of the five novelty axes.
- `Baseline_CurrentProject`: the explicit no-novelty baseline.

With ten scenarios and two seeds for the novelty-combination space, plus one no-novelty baseline run per scenario, the default count is:

```text
32 novelty combinations * 10 scenarios * 2 seeds = 640
1 no-novelty baseline * 10 scenarios * 1 seed = 10
Total = 650
```

`Combo_00000` and `Baseline_CurrentProject` are intentionally different:

- `Combo_00000` means the experiment harness/proposed runtime path with all novelty flags disabled.
- `Baseline_CurrentProject` means the explicit no-novelty neutral baseline.

Generated cases include `combination_bits_6`:

- First five bits: the five novelty axes in the order listed above.
- Last bit: `enableNoNoveltyBaseline`.

Examples:

```text
Combo_00000              -> combination_bits_6 = 000000
Combo_11111              -> combination_bits_6 = 111110
Baseline_CurrentProject  -> combination_bits_6 = 000001
```
