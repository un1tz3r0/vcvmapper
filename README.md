# Puppeteer

VCV Rack 2 modules that attach CV to any other module's knobs, switches, buttons and lights, in both directions, without changing the target plugin.

## Modules

**Pin** (4 HP) binds one jack to one or more targets.

1. Press **LEARN**, then touch a knob or switch on another module, or click one of its lights. *Learn another* in the context menu adds more.
2. **IN** drives the bound knobs. A mono cable drives them all; a poly cable drives one target per channel. **DEPTH** scales and inverts it.
3. **OUT** reports each target's current value, one channel per target: the knob position, or a light's brightness as 0 to 10 V.

By default CV modulates around wherever your hand last left the knob, and pulling the cable hands the knob back. The context menu has *Overwrite* for MIDI-Map style control and *Pick up*, which waits for the CV to reach the knob before taking over and lets go when your hand moves it. Momentary buttons follow a gate, held down for at least *Button hold*.

Several Pins can drive the same knob: their modulation adds up around the one hand position. Puppeteer never takes a knob that MIDI-Map or another mapper holds; it waits until they let go. *Unmap* on the knob's own menu removes it from the Pin.

**Paths** bind every matching target, re-matched as modules come and go. A path is `plugin/model:param`, with several separated by commas. `*` and `?` match any text, `#3` or `#0-7` pick Rack's param ids, and `light:` reads lights instead. For example `Fundamental/VCO:Freq*` drives the frequency knob of every VCO, left to right and top row first. *On every …* in a target's menu turns a learned knob into such a path. The *Path* menu builds one from menus of what is in the rack, with a count of what each choice would reach, and while it is open every matched control is outlined on the rack.

A path can say where to look with `@`: `VCF@row:Cutoff` reaches only VCFs on the module's own row, `@column` those above and below it, and `@group` those in its group, a run of modules touching side by side. **Twins** do the same for learned targets: learn one voice's cutoff, set *Twins* to *row*, and the same knob on the same model, at the same place in every other group on the row, follows too, so the second VCA of each voice pairs with the second VCA of every other. A binding drives up to 64 targets, each following input channel *i*, wrapping round the channels the cable has. **Spread** fans them out across the knob's travel, first to last, for detuned voices.

Moving modules can change what a path or twins reach. When the count of targets differs from when you last edited or loaded it, the screen says so until you choose *Accept* in the menu.

**Shape** maps the CV onto part of the knob's travel, with a curve, inversion and steps. Params are written every 32 samples, or every sample while the CV runs at audio rate.

**Marionette** (14 HP) is eight Pins behind one screen. Each row of the screen is a channel, between its **IN** and **OUT** jacks: what it drives, a bar for the target's position with a mark at your hand's, and its mode.

1. Click a row, or turn the left knob, to select a channel. The footer of the screen always says what the two knobs do.
2. Press **LEARN** and touch a knob or light. Learning carries on down the channels while they are empty, so you can map several in one go; press **LEARN** again to stop.
3. The right knob sets the selected channel's depth. Press **EDIT** for the rest of its settings: the left knob picks one, the right knob changes it. The first, *PATH*, opens the path menu when clicked.

Everything Pin's menu offers (more targets, paths, twins, shape, timing) is in the context menu for the selected channel; right-click a row to open the menu for that channel.

## Building

```sh
make RACK_DIR=/path/to/Rack-SDK install
```

`tools/mockup` draws the panels as SVG without Rack, and `tools/enginetest` runs the modules against Rack's engine built from source. CI builds every platform on each push; tagged releases (`v*`) collect the `.vcvplugin` files in a draft GitHub release.
