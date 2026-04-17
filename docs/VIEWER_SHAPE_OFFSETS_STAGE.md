# Viewer Shape Offset Stage

This stage extends the DX11 viewer quick edit form with direct shape-offset controls for the selected edit layer.

Added controls:
- Root Offset X / Y / Z
- Tip Offset X / Y / Z
- Side Offset
- Yaw

Behavior:
- The selected inline layer now syncs root/tip offsets, side offset, and yaw into the quick edit form.
- `Replace Selected` and `Append Layer` preserve these fields when serializing back to the inline CSV.
- The preview summary now includes center, tip Z, and yaw hints so the scalp overlay is easier to interpret while tuning a layer.

Notes:
- Core / CLI remain Linux-buildable.
- DX11 viewer implementation was updated, but Windows runtime verification is still pending.
