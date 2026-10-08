# Puppeteer

VCV Rack 2 modules that attach CV to any other module's knobs, switches, buttons and lights, in both directions, without changing the target plugin.

## Modules

**Pin** (4 HP) binds one jack to one target.

1. Press **LEARN**, then touch a knob or switch on another module, or click one of its lights.
2. **IN** drives the bound knob. **DEPTH** scales and inverts it.
3. **OUT** always reports the target's current value: the knob position, or the light's brightness as 0 to 10 V.

By default CV modulates around wherever your hand last left the knob, and pulling the cable hands the knob back. Switch to *Overwrite* in the context menu for MIDI-Map style control.

## Building

```sh
make RACK_DIR=/path/to/Rack-SDK install
```

CI builds every platform on each push; tagged releases (`v*`) collect the `.vcvplugin` files in a draft GitHub release.
