# Design: scopes, groups, cord targeting and the hub

Phases 4 to 9 of `TODO.md`. Module names other than Pin and Marionette are working names.

## One idea underneath

A binding's selector answers two questions: which control, and on which instances. Phase 2 answered the first with
patterns (`plugin/model:item`) and the second with "every match in the rack". Sibling multi-mapping, group scoping and
cord targeting are all better answers to the second question, so they share one mechanism: a **scope** on each clause.

```
clause := [plugin/]model[@scope]:[light:]item  |  $tag:[light:]item
scope  := all | row | column | group | cable | <glob over group names>
```

| Scope | Instances it admits |
| --- | --- |
| `all` (default) | every module in the rack, as today |
| `group` | modules in the same group as the module that owns the binding |
| `row` | modules in groups on the anchor's row |
| `column` | modules on any other row that overlap the anchor's horizontal span, plus the anchor's own |
| `cable` | modules at the far end of the owner's selecting jacks (Tether only) |
| `Voice*` | modules in groups whose name matches |

A group named like a keyword is written in quotes (`@"row"`). `$tag` names the single module a Tag points at and takes
no scope.

The **anchor** is the owning module for a typed clause and the learned module for a learned target. Learned targets
stay keyed by module id in `Selector::targets`, and `Settings::twins` (off, row, column or rack, off by default) adds
their twins. It lives in the settings rather than the selector so the screens' setting lists can edit it.

### Groups and twins

A group is a horizontal run of modules on one row:

- **explicit**: from a Troupe header to its end spacer, or to the first gap or next header when there is no spacer;
- **implicit**: any run of touching modules outside an explicit group.

The **twin** of module M in another group is the module with M's model and M's ordinal among that model, so the second
VCA of one voice pairs with the second VCA of every other. An extra module in one voice shifts nothing, which a plain
position index would not survive. `column` has no groups to lean on across rows and uses horizontal overlap alone.

Lanes are ordered left to right, then top to bottom, so poly channel *i* drives voice *i* and a mono source drives all.
Moving a module can change what a scope admits. That is the point, but it is also a quiet way to rewire a patch, so
the overlay below shows the matches whenever a binding is being edited, and a binding whose match count changed since
the patch was saved flags it on its screen row until acknowledged.

### Spread

`Settings::spread`, −1 to 1. Lane *i* of *n* is offset by `spread · (i/(n−1) − ½)` of the control's travel, applied
after the shape, while the CV is plugged in. Identical voices are where detune is wanted, and lanes make it nearly free.

### More than 16 targets

`Plan::lanes` is sized by `PORT_MAX_CHANNELS` only because a lane was a poly channel. It becomes `MAX_LANES = 64`, with
lane *i* reading channel `i mod channels`. `Slot::CELLS` (bindings per param) is unrelated and stays 16.

## Path editor and overlay

Typing globs blind is how wildcards go wrong, so the editor is a breadcrumb of menus, one per component (scope, plugin,
model, item), each listing what the rack holds now with a match count, and each topped by a text field for a glob.
Items show as `#N name`, because param names are a poor key: many modules leave them empty or repeat them. Choosing an
unnamed or ambiguous item writes `#N`. It lives in `kit/Menus.hpp` and is used by every module with an EDIT page.

The **overlay** is a transparent widget the editing module adds to `APP->scene->rack` and removes when editing ends or
the module is deleted. It outlines each resolved control in the binding's colour (`ModuleWidget::getParam(id)` gives
the box) and brackets each group in play.

## Group modules

- **Troupe** (header, 4 HP): a name field and four macro knobs. Its bindings are locked to `@group`, so a cloned voice
  keeps working without a remap. A menu item selects the whole group, which hands duplicate, save and delete to
  Rack's own selection commands.
- **Wing** (end spacer, 1 HP): closes the group. No controls.
- **Tag** (2 HP): a name and a left or right switch. Names the neighbour it touches (`Module::leftExpander` or
  `rightExpander`) for `$name` clauses.

Group and tag lookup is a UI-thread registry rebuilt when a module is added, removed or moved, so resolving a clause
never walks the rack more than once per change.

**Collapsing is out.** Rack has no hidden module. Faking one means hiding widgets and moving neighbours by hand, and
that fights undo, selection, cable drawing and every other plugin that reads positions. Macros on the header give the
Combinator's use without its fold.

## Tether: targeting by cord

A cord names a module and a port, never a control, so the cord supplies the scope and a path or learn supplies the
item.

Jacks: `IN`, `THRU`, `CV`. `THRU` copies `IN`, every channel, one sample late like any Rack hop. The targets are the
modules on the far end of `THRU`'s cables, or of `IN`'s cable when `THRU` is unpatched; a stacked output gives one lane
per cable. `IN` is therefore also the fallback source: patch only `THRU` to pick a target, and whatever feeds `IN`
reaches it.

`Module::onPortChange` sets a flag on the engine thread; the widget's `step()` then reads
`RackWidget::getCompleteCablesOnPort` and rebuilds the plan. Learning a control stores its model and id. On a module of
the same model the id is reused; on another model the item is matched by name, and a miss leaves the binding unbound
with the reason on screen.

## Stage: the hub

Replaces the screen multi-mapper planned for the old phase 4. Fixed width, around 30 HP: Rack fixes port counts at
construction, so growth comes from jack expanders and from sources that need no jack.

- **Panel**: a large screen; soft buttons along the top (pages) and left (actions); four encoders below and four to
  the right, relabelled by the page; a d-pad with select, cancel and shift; eight inputs and eight outputs.
- **Bindings**: a fixed pool of 64, each fed by a jack, an expander jack or a macro. A pool rather than a growing
  vector keeps the audio thread's view stable under the existing plan hand-over.
- **Pages**: Bindings (list), Edit (path, mode, shape, spread, with a rolling history of source and result), Macros,
  Groups. A waterfall view follows once the history graph has proved the buffer.
- **Macros**: an encoder value as a source. One macro fans out to many bindings, each with its own shape, which is the
  range mapping a snapshot morph needs later.

### Keyboard

Rack sends keys to `APP->event->selectedWidget` through `onSelectKey` before anything else, and a widget becomes
selected by consuming a left click. The screen and d-pad do so, then consume arrows, Enter, Escape and Backspace, and
read Shift from `e.mods`. Consuming is what stops `Scene` scrolling the rack with the arrows. Chords with Ctrl are left
alone so Rack's shortcuts keep working.

Limits to design around: any click elsewhere drops the selection, there is no way to hold it, and a module cannot see
keys while another widget is selected. So focus shows as a lit bezel, every keyboard action has a panel control, and
nothing is reachable by keyboard alone.

## Later: snapshots and the recorder

Both capture sets of controls, and a path already names a set: `*@group:*` is "this voice". Snapshots store the
resolved values per lane and recall or morph through the binding core, so hand, CV and morph combine in one slot.
The recorder writes the same lanes over time. Neither needs a new way to say what it acts on.
