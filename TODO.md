# Puppeteer roadmap

The full plan, with the reasoning behind it, is the "vcvmapper plan" doc. Each phase is complete once it is merged and
tested in Rack (see the versioning rules in `CLAUDE.md`).

1. **Scaffold, CI and Pin.** One binding, CV to a param or a light to CV, learn by touch or click, modulate or
   overwrite, paste and undo safe. CI builds every Rack target.
2. **Binding core.** A shared registry, write modes, pick-up, per-binding write rate with audio-rate detection, button
   hold time, polyphonic fan-out, wildcard and multi-instance selection, persistence.
3. **Kit and the 8-channel module.** Grow the kit into reusable controls and screens; an 8-binding module with and
   without advanced settings.
4. **Screen multi-mapper and expanders.** A menu-driven mapper for any number of bindings, with input and output jack
   expanders that populate its source and sink lists.
