# Lua API compatibility gate

Use the matching branch for the change. Keep checks limited to namespaces,
classes, and callers touched by the task.

## Interface-sensitive changes

Use this branch when editing `LV_SCHEMA`, `LV_BINDING`, binding registration, or
another declaration that can change the generated Lua API.

1. Build the owning target from the current tree so
   `.cache/lv_meta/lv_api.lua` is current.
2. Record the affected constructors, methods, properties, and ports.
3. After the change, rebuild and compare only that affected public surface.
4. Accept requested additions. Preserve existing names and signatures unless
   the task explicitly requests a migration.
5. For an explicit migration, update the affected callers and tests identified
   during inspection.

**Criterion:** Every affected API delta is required by the task, and every
identified caller remains valid or is included in the requested migration.

If unrelated working-tree changes prevent a clean baseline, record that
limitation and verify the expected symbols directly in the regenerated API and
the smallest affected caller.

## Wiring-only changes

Use this branch for Lua hierarchy changes that do not edit an API-producing
declaration.

1. Inspect the diff and confirm it is limited to construction, configuration,
   or connectivity.
2. Run the smallest existing Lua construction or elaboration path that uses the
   modified hierarchy.

**Criterion:** No API-producing declaration changed, and the modified hierarchy
elaborates through an existing caller.
