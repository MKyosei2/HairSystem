# Autonomous QA Bug Zoo

This directory defines a deterministic benchmark for the autonomous QA layer itself.
It does not claim that an AI model is correct because it produced a plausible patch.
The benchmark scores whether the system selected the mandatory experiment classes,
produced required evidence, rejected unsafe candidates, and avoided patching the clean
control when the baseline does not reproduce a defect.

`manifest.json` is the source of expected experiment selections. Export autonomous-QA
run observations into a JSON file accepted by `tools/autonomous_qa_bug_zoo_verify.py`.
A result row has this shape:

```json
{
  "caseId": "memory-growth",
  "selectedExperiments": ["Soak", "MemorySafety", "RegressionTest"],
  "requiredEvidenceComplete": true,
  "unsafePatchAccepted": false,
  "reproduced": true,
  "patchGenerated": true
}
```

For `clean-control`, `patchGenerated` should be false when the exact-revision baseline
passes. The verifier reports selection recall/precision, evidence completion, unsafe
patch acceptance, reproduction rate, and clean-control behavior. A release claim
should be based on measured output from the same revision being submitted, not this
manifest alone.
