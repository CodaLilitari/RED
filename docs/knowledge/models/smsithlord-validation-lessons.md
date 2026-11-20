# Animated skins, sound capacity and target-engine review

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/smsithlord-validation-lessons.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Original workflow notes informed by **SM Sith Lord's COG Factory**, reviewed on
2026-10-25. Source-page runtime results are author reports, not tests rerun by
this workshop. These procedures apply to the next authorized asset; they do not
resume a paused character build or certify an existing one.

## Judge a skin in motion, with its node roles intact

[P76, four20ip Skin Pack](https://smsithlord.com/openjkdf2libretro/cogfactory/gen2/p76-f20ip-skins.html)
describes evaluated pose-bone positions, joint extensions, stock-animation
coverage and a body piece conflicting with a weapon-node role. It distinguishes
successful imports from an export round trip that changed hierarchy
representation. We apply those review lessons without importing the pack's
meshes or converter.

Retain the editable source and a separate export copy. Identify the actual rig,
weapon attachment domain, animation set and indices from its files. Do not
promote one reported stock node number into a universal custom-rig rule. Compare
evaluated and rest pose data deliberately and record which supplied the pivots.

The current read-only inspector supports:

```text
python skills/jk-mots-model-workflow/scripts/inspect_contract.py --model candidate.3do --keys idle.key walk.key fire.key death.key
```

Supply the exact candidate KEYs for all relevant states. For appended details,
`--reference-keys` accepts original unpadded KEYs and `--detail-start` identifies
the first proposed detail node. Read `--help` before that optional check. The
inspector checks hierarchy indices, parent cycles and KEY allocation; it does
not validate motion, grips, seams or sound.

Review idle, locomotion, attack, jump/fall, death and relevant weapon transitions.
Inspect caps and intersecting joint extensions at motion extremes. A frozen
animation control helps reject measurements of camera/background motion alone.
Match exported hashes to the engine test. Successful reimport is separate proof.

## Treat audio as a resource and saved-profile contract

P76 reports that an SND/WAV-only attempt exhausted a level sound pool, its final
build increased static allocations, and an older saved character retained a
separately stored voice. Its later retail voice observation is narrower than
complete retail multiplayer or listening approval.

Enumerate selected SND events and reachable WAV variants with source, intended
event, format and hash. Decode each output to check duration, channels and sample
format, then test audible output separately. An event counter is not a listening
test. Compare sound and sound-class allocations against the full mounted
profile; adding WAVs alone does not reserve slots. Preserve existing registration
indices when preparing a reviewed capacity change. Keep an exhausted-allocation
control and distinguish resource counts from simultaneous playback channels.

Test both a fresh character and the actual upgrade path of a saved one. Check
other providers of `jkl/static.jkl` and `misc/models.dat`: these resources do not
implicitly merge their rows. Do not transfer P76's unmodified-client fallback
report to MotS without a connected-peer test.

## Follow event dependencies and inspect the actual renderer

[P50](https://smsithlord.com/openjkdf2libretro/cogfactory/gen1/p50-walker-yard.html)
and [P52](https://smsithlord.com/openjkdf2libretro/cogfactory/gen1/p52-dark-jedi-gauntlet.html)
show that inherited templates and class/placed COGs can reach models, effects and
sounds through later events. Track source identities and mark dynamic references
unresolved until their relevant paths are exercised in an isolated profile.

[P77, AIrena](https://smsithlord.com/openjkdf2libretro/cogfactory/gen2/p77-airena.html)
reports useful mechanics checks but an unacceptable architecture-lighting result.
Apply that distinction to skins: keep source renders, exported-file checks,
engine appearance, animation, connected-peer behavior and retail evidence as
separate verdicts. This note adds procedures, not exporter features or new runtime
results.
