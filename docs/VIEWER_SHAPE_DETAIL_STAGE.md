# Viewer Shape Detail Stage

This stage extends the DX11 viewer quick-edit form so a selected edit layer can be adjusted beyond region placement and offsets.

Added controls:
- Width Scale
- Follower Influence
- Group Filter
- Guides Only toggle

Behavior:
- Selected inline layer now syncs widthScale, followerInfluence, groupId, and guidesOnly into the quick-edit form.
- Replace Selected / Append Layer preserve those values back into the inline CSV edit stack.
- Layer list rows summarize width/follower values and show group filters when present.
- Edit Influence preview summary now includes width, follower influence, group filter, and whether the layer is guide-only.

Notes:
- Group Filter accepts blank / `Any` / `*` for no restriction.
- Viewer implementation updated only; Linux validation still covers core / CLI build paths.
