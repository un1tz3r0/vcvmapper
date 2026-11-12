# Puppeteer

VCV Rack 2 modules that attach CV to any other module's knobs, switches, buttons and lights, in both directions, without changing the target plugin.

## Modules

**Pin** (4 HP) binds one jack to one or more targets.

1. Press **LEARN**, then touch a knob or switch on another module, or click one of its lights. *Learn another* in the context menu adds more.
2. **IN** drives the bound knobs. A mono cable drives them all; a poly cable drives one target per channel. **DEPTH** scales and inverts it.
3. **OUT** reports each target's current value, one channel per target: the knob position, or a light's brightness as 0 to 10 V.

By default CV modulates around wherever your hand last left the knob, and pulling the cable hands the knob back. The context menu has *Overwrite* for MIDI-Map style control and *Pick up*, which waits for the CV to reach the knob before taking over and lets go when your hand moves it. Momentary buttons follow a gate, held down for at least *Button hold*.

Several Pins can drive the same knob: their modulation adds up around the one hand position. Puppeteer never takes a knob that MIDI-Map or another mapper holds; it waits until they let go. *Unmap* on the knob's own menu removes it from the Pin.

**Patterns** bind every matching target, re-matched as modules come and go. A pattern is `plugin/model:param`, with several separated by commas. `*` and `?` match any text, `#3` or `#0-7` pick Rack's param ids, and `light:` reads lights instead. For example `Fundamental/VCO:Freq*` drives the frequency knob of every VCO, left to right and top row first. *On every …* in a target's menu turns a learned knob into such a pattern.

**Shape** maps the CV onto part of the knob's travel, with a curve, inversion and steps. Params are written every 32 samples, or every sample while the CV runs at audio rate.

## Building

```sh
make RACK_DIR=/path/to/Rack-SDK install
```

CI builds every platform on each push; tagged releases (`v*`) collect the `.vcvplugin` files in a draft GitHub release.
