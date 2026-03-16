# WING OSC Protocol Notes

This document is a practical protocol reference for AUDIOLAB.wing.reaper.virtualsoundcheck.

It is split into:

- Implemented subset: OSC addresses and flows used by this plugin
- Extended reference: additional WING OSC paths that may be useful but are not guaranteed by this codebase

## Transport Basics

- Protocol: OSC 1.0 over UDP
- Default WING port: `2223` (configurable on console)
- Address form: `/node/...`

## Implemented Subset (Plugin-Relevant)

AUDIOLAB.wing.reaper.virtualsoundcheck relies on core channel metadata and routing-related queries to:

- discover channels
- read/update names and colors
- inspect source/routing state
- support virtual soundcheck ALT source workflows

Representative addresses used in plugin workflows include:

- `/ch/[XX]/config/name`
- `/ch/[XX]/config/color`
- `/ch/[XX]/config/source`
- `/ch/[XX]/config/link`

Channel indexes follow zero-padded formatting (`01` .. `48`).

## Query Pattern

Typical OSC query pattern:

- Send address without arguments to request current value
- Parse response and map into channel model
- Retry/timeout handling is required (UDP is unreliable)

## Subscription and Monitoring

For real-time updates, subscription style endpoints may be used where available.

Important notes:

- treat packet loss as normal
- implement conservative query pacing
- avoid flooding WING with tight polling loops

## Extended Reference (Not Full Plugin Contract)

The WING OSC ecosystem contains many additional paths beyond what this plugin requires (mixing, preamp, scene, FX, etc.), for example:

- `/ch/[XX]/mix/fader`
- `/ch/[XX]/mix/on`
- `/ch/[XX]/mix/pan`
- `/scene/*`
- `/config/*`

These are useful for future features, but should not be interpreted as fully implemented behavior in AUDIOLAB.wing.reaper.virtualsoundcheck.

## Sources and Validation

- Behringer WING official documentation
- Community protocol references (including Patrick-Gilles Maillot material)
- Empirical validation in this codebase through integration behavior

When adding new OSC features, verify addresses and value ranges against live console behavior before documenting them as supported.
