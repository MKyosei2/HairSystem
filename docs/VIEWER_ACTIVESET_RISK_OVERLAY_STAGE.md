# Viewer Active-Set Risk Overlay Stage

Added a new scalp overlay mode, **Active-Set Risk / Selection**, to preview the runtime danger scoring directly in the DX11 viewer.

## Added
- `Scalp Overlay -> Active-Set Risk / Selection`
- risk heat visualization driven by `m_activeSetPlan.scores[*].finalScore`
- selected active-set clusters get an extra tint so chosen regions are obvious
- active / inactive boundaries get a light rim for easier inspection
- overlay refreshes automatically after active-set runtime evaluation
- settings save/load supports the new overlay enum value

## Reading the overlay
- cool / blue regions: lower danger score
- warm / red regions: higher danger score
- yellow-green boosted regions: currently selected by the active-set runtime
- pale rim: transition boundary between active and inactive guide neighborhoods

This stage makes the proposal's danger-region selection visible while tuning bounded-latency active-set behavior.
