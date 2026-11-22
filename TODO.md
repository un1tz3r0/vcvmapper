# Puppeteer roadmap

The original plan, with the reasoning behind it, is the "vcvmapper plan" doc; phases 4 onward are designed in
`docs/design.md`. Each phase is complete once it is merged and tested in Rack (see the versioning rules in `CLAUDE.md`).

1. **Scaffold, CI and Pin.** One binding, CV to a param or a light to CV, learn by touch or click, modulate or
   overwrite, paste and undo safe. CI builds every Rack target.
2. **Binding core.** A shared registry, write modes, pick-up, per-binding write rate with audio-rate detection, button
   hold time, polyphonic fan-out, wildcard and multi-instance selection, persistence.
3. **Kit and Marionette.** Grow the kit into reusable controls and screens; an 8-binding module with and without
   advanced settings.

## 4. Scopes, paths and the overlay

- [x] `Pattern.hpp`: parse `@scope` and `$tag`; keep old patterns valid. Tests in `tools/`.
- [x] `core/Groups`: implicit groups (touching runs), ordinals per model, twin lookup; rebuilt on add, remove and move.
- [x] `Binding::resolve`: apply clause scopes; `Settings::twins` expands learned targets to their twins.
- [x] `MAX_LANES = 64`, lane *i* reads channel `i mod channels`.
- [x] `Settings::spread`, applied per lane after the shape.
- [x] `core/BindingUi`: breadcrumb path editor with live lists, match counts and `#N name` items.
- [x] Overlay widget on the rack: outlines matched controls and brackets groups while editing.
- [x] Changed-match flag on a binding's screen row.
- [x] Marionette and Pin: path row and scope and spread settings on the EDIT page.
- [ ] In Rack: a four-voice patch of identical chains, one knob learned, driven mono and poly, voices dragged around.

## 5. Groups and tags

- [x] Troupe header: name field, group registry entry, "select group" menu item.
- [x] Wing end spacer; a group without one ends at the first gap or next header.
- [x] Tag: names its left or right neighbour for `$name` clauses.
- [x] Named-group scopes (`@Voice*`) and `@group` in the resolver and the path editor's scope menu.
- [x] Troupe macros: four knobs with bindings locked to `@group`.
- [ ] In Rack: clone a group with Rack's selection clone and confirm the copy's macros drive the copy.

## 6. Tether

- [ ] Module with `IN`, `THRU`, `CV`, depth and a one-line screen; poly pass-through.
- [ ] `cable` scope from `onPortChange` and `getCompleteCablesOnPort`; `THRU` side wins, stacked cables become lanes.
- [ ] Learn that survives re-patching: same model by id, other models by name, unbound with a reason otherwise.
- [ ] In Rack: move the cord between modules of the same and different models, in both directions.

## 7. Stage, the hub

- [ ] Layout and mockup: screen, soft buttons, eight encoders, d-pad, 8 in and 8 out.
- [ ] Binding pool of 64 with jack, expander and macro sources.
- [ ] Screen framework: pages, soft-button and encoder labels, cursor.
- [ ] Keyboard focus: selection on click, `onSelectKey` for arrows, Enter, Escape, Backspace and Shift, lit bezel.
- [ ] Pages: Bindings, Edit, Macros, Groups.
- [ ] History ring buffer per binding and the rolling graph on the Edit page.
- [ ] Input and output jack expanders.
- [ ] Waterfall view.
- [ ] In Rack: build and edit a patch by keyboard alone, then by panel alone.

## 8. Snapshots and morph

- [ ] Capture sets as paths; store, recall and crossfade between snapshots through the binding core.

## 9. Recorder

- [ ] Record lanes as curves; crop, loop, one-shot and triggered playback.
