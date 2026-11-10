# ET-Core: staged MQTT replacement

The goal is a complete replacement for the owner's MQTT-dependent Home Assistant
workflows, through verified migrations. Native media-player support is the next
milestone, not evidence that the whole goal has been achieved.

## Rules for every migration

- Back up the working firmware, dependency versions, integration settings, entity
  registry and dependent dashboards/automations before a change.
- Preserve real features and canonical entity IDs. Do not report an accepted
  command as completed action; state comes from the device.
- Use the encrypted native ET-Core transport, authenticated discovery, command
  IDs, replay rejection and leases. Do not hide MQTT under a renamed adapter and
  count that as a completed native migration.
- Publish generic examples only. Real device definitions, Wi-Fi details, keys,
  diagnostic snapshots and configured firmware stay private.
- Compile the exact board/core/dependencies and retain a tested rollback path.
  Hardware operation, streaming and OTA each need their own verification.

## Current milestone: native media + protected OTA

See [media-player.md](media-player.md) for the protocol, Home Assistant platform,
WROVER example, build target, OTA layout and per-requirement acceptance gates.
Keep the working MQTT player active while this is staged. Preserve Music
Assistant's existing references during the final entity handover.

## Remaining replacement work

1. Maintain a private inventory of actual MQTT producers, consumers, discovery
   records and dependent HA entities. A native gateway does not prove every
   downstream sensor or automation has migrated.
2. Cover missing native device classes and their metadata, settings and actions
   based on that inventory. Add repeatable platform/transport/device tests.
3. Migrate one device or gateway at a time with owner-visible verification.
   Record firmware, schema, preserved entity IDs, rollback and runtime evidence.
4. Handle MQTT-only third-party systems explicitly. They require a supported
   native integration, a firmware change or a separately reviewed replacement;
   retaining an MQTT hop is a transition step rather than final completion.
5. Remove legacy declarations, retained discovery and the broker only when
   observation proves no remaining dependency. Do not disable a broker merely
   because the currently tested native endpoint works.

Completion requires verified feature parity, preserved references, working
recovery/updates and no remaining MQTT dependency within the agreed inventory.
