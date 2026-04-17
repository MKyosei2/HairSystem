# DX11 viewer edit-stack stage

This stage wires the proposal-aligned edit stack into the DX11 viewer workflow.

## Added viewer behavior

- `Load Edit Stack...` button in the viewer UI
- `Clear Edit` button to restore the unedited base asset
- automatic re-apply of the current edit stack after `Rebuild Hair`
- `.hairbin` / `.hair` import support in the viewer import dialog
- settings persistence for `editStackPath`
- scalp overlay mode `Edit Influence`
- inline edit-stack editor inside the viewer
- `Apply Inline`, `Save Inline...`, and `Reset Inline` actions
- settings persistence for the inline edit-stack text

## Typical demo flow

1. Import a `.csv`, `.usda`, `.usd`, or `.hairbin` hair asset.
2. Either load `docs/sample_assets/edit_stack_template.csv` or edit the inline stack text directly in the viewer.
3. Press `Apply Inline` to push the inline stack into the runtime.
4. Toggle scalp overlay to `Edit Influence` to inspect the affected guide region.
5. Press `Rebuild Hair` after changing guide/follower counts; the same edit stack is re-applied.
6. Use `Save Inline...` to export the current inline stack as a normal CSV.
7. Use `Clear Edit` to revert to the stored base asset.

## Notes

The viewer stores the untouched imported/procedural asset as the base asset, then applies the edit stack on top of that copy. This keeps the proposal's “broad area select + local refinement + runtime active-set coupling” flow visible inside the runtime viewer.

The inline editor intentionally uses the same CSV row format as the CLI path. That means a stack authored in the viewer can be saved out and reused by `hairgpu_cli`, and a stack authored in the CLI pipeline can be loaded back into the viewer and tweaked in place.
