# Aim Ring input assist

Lost Odyssey's Aim Ring update enters phase 4 with an outer ring size that remains at its initial value until the guest sees a non-zero right trigger. On affected hosts this makes the shrinking outer ring effectively invisible unless RT is already being held.

The runtime now uses the guest Aim Ring phase already observed by `sub_82B15960` to provide a small input compatibility path:

- If phase 4 begins **with RT already held**, input is unchanged. The original hold-then-release control remains intact.
- If phase 4 begins **with RT released**, the runtime feeds RT=255 so the outer ring starts shrinking.
- The first physical RT press is translated to RT=0 for the guest, producing the release/judge edge.
- Leaving phase 4 resets the helper state.

Set `LO_RING_ASSIST=0` to disable the compatibility path and use unmodified trigger input.

## Platforms

The implementation lives in the common SDL/XInput-compatible HID path and has been exercised on:

- Windows x64
- SteamOS / Linux x64
- ROCKNIX / Linux AArch64

No platform-specific renderer change is required for the Aim Ring itself.

## Diagnostics

`LO_RING_TRACE=1` logs the guest ring object after its update. Useful fields are `phase`, `input`, `progress`, and `size`.

The compatibility path only changes RT while the guest reports Aim Ring phase 4; ordinary movement, menus, and other battle input are unaffected.
