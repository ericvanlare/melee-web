# Pinned SDL controller identity probe

`webmelee-controller-probe` is a short-lived, read-only readiness helper for
the reference-capture app. It links to the already-built SDL 3.4.4 archive
used by the pinned Dolphin build and follows Dolphin's SDL backend enumeration
boundary:

* initialize joystick, haptic and gamepad subsystems;
* enumerate `SDL_GetJoysticks` instance IDs;
* open each joystick and its mapped gamepad using the same SDL APIs as
  `Source/Core/InputCommon/ControllerInterface/SDL/SDL.cpp`;
* use `SDL_GetGamepadName` with Dolphin's `SDL_GetJoystickName` fallback;
* assign `index` as the zero-based occurrence of that exact display name;
* read VID/PID, GUID, instance ID and `SDL_IsJoystickVirtual`; and
* close every handle and quit SDL.

The probe never reads serials or device paths, changes a control, sends rumble,
or runs concurrently with Dolphin. Its output is one JSON object on stdout:

```json
{
  "schema": "webmelee-controller-probe",
  "version": 1,
  "status": "ok",
  "config": {
    "provided": true,
    "gc_adapter_configured": false,
    "gc_adapter_hint": "1"
  },
  "devices": [
    {
      "name": "Nintendo GameCube Controller",
      "index": 0,
      "instance_id": 1,
      "vendor_id": 121,
      "product_id": 6211,
      "guid": "...",
      "virtual": false
    }
  ]
}
```

`--config PATH` accepts a Dolphin `Dolphin.ini`. The probe applies values in
the same order as Dolphin: its six SDL defaults, the `SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE`
selection implied by any `Core/SIDevice0..3 = 12`, and explicit values from
the `[SDL_Hints]` section. The caller must sanitize inherited `SDL_*`
environment variables just as it does for Dolphin.

The reproducible builder is `scripts/build_reference_controller_probe.py`.
With the checked-out reference workspace it reuses
`work/reference-dolphin-build-v2/Externals/SDL/SDL/libSDL3.a` and the bundled
`work/reference-dolphin-build-v2/Externals/libusb/libusb.a`; otherwise it
fails closed instead of silently selecting a system SDL. It writes the
executable and a provenance manifest below the ignored
`work/reference-controller-probe-v1/` directory.

The source inputs are available in the pinned checkout at
`work/reference-dolphin-source-v2/Externals/SDL/SDL` (SDL revision
`5848e584a1b606de26e3dbd1c7e4ecbc34f807a6`) and the Dolphin checkout at
revision `c77bbaa0f372c3f72281602a8b087206706542cb`. SDL's notice and license
are in its `LICENSE.txt`; Dolphin's corresponding source and notices are kept
in that checkout. The build manifest records these revisions, input hashes,
probe source hashes, compiler identity/options, and the Mach-O runtime
inventory used for installation verification.
