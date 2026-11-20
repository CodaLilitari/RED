# Generated character voices: Suno takes to a JK sound class

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/generated-character-voice.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Use this route when a model needs its own short hurt, death, falling or breathing sounds. Generate a source performance, select and edit the useful moments, then bind them to the actual game events. A successful song-generation request is not a finished voice bank. This chapter draws on the November 2026 Darth Lilitari collaboration: Lily/Lilitari directed the voice and wrote the Suno prompts; Red/Claude generated, cut, levelled, integrated and tested the material; Andrei supplied direction, listened, corrected a word boundary and chose the final use of “Again”. Coda reviewed the retained evidence and extracted the method here.

## Start with the engine's request

Before generating, inspect the target sound class, its callers, existing stock durations and available sound slots. Build a small table of event, selection rule, repetition, interruption, priority, desired duration and current placeholder. Keep voice separate from footsteps, weapon effects, impact sounds and other foley.

For the reviewed JK player class, three hurt intensities, a randomized death pool and a single falling scream supplied the first useful set. Water damage, underwater death and surfacing need their own physical performances. AI alert/gloat slots do not create a player taunt button. MotS, a source port and a modded puppet/class need their own caller check; do not infer their behavior from the slot names.

Two source-to-runtime corrections changed this project:

- Red's initial brief described Death 2 as an overkill event. Later retail inspection and tests reported that the relevant JK 1.01 kill path zeroes health before the overkill comparison, so the intended alternate is unreachable there. The final class puts every selected death in Death 1 and mirrors the pool under Death 2 for compatibility. This is a version-specific reported finding, not a rule inferred from the label.
- Hurt clips are selected by severity; they are not an arbitrary randomized pool. The final v1.2 puts `dlagain.wav` in the heavy row of all five hurt classes. The original heavy cry remains in the fourth row, reported unreachable in that retail path. Keep the exact version and row semantics beside the assets.

## Generate short, separable performances

1. **Define one voice.** Specify register, consonants, accent, texture and attitude in performance terms. Retain the same core direction across batches. Avoid solving an acting mismatch by adding distortion or turning the take down.
2. **Split by event family.** Generate core hurts, death alternatives and falling separately; add water and specialized damage families when needed. Ask for clear gaps and describe each reaction's intensity and approximate length.
3. **Use the service's actual fields.** Red's saved Suno v6 session used a positive Styles description and a separate Exclude styles field because the Styles box then capped at 1,000 characters. Bracketed lyrics instructions were treated as natural-language direction, not a guaranteed command vocabulary. Shortened benign directions were accepted after two longer prompts were rejected. Preserve the actual submitted text and any transformation; current controls and limits must be checked anew.
4. **Keep the source takes.** The recorded run used 20-second takes, two per batch, female voice, Style Influence 75% and Weirdness 35%. These are historical settings, not an optimum or a promise that today's product exposes the same options. Four selected source WAVs were 48 kHz stereo. Plan limits allowed only four downloads at that point, so generation, export and completed coverage were different states.
5. **Review the whole take before cutting.** Suno produced extra reactions, changed the requested ordering, widened the voice and added low-frequency cinematic beds despite the dry-voice direction. Lily selected early clean windows and discarded contaminated tails. Select by the performance that arrived; the requested light/medium order was reversed in the actual first batch.

A compact adapted example, not a verbatim project prompt:

```text
Styles: Dry close-mic video-game voice acting. One consistent adult performer.
Clear consonants, controlled combat effort, mono-compatible central voice.

Exclude styles: Music, instruments, rhythm, melody, choir, vocal doubling,
room reverb, delay, environmental effects, spoken labels.

Lyrics: [Three separate nonverbal reactions; leave silence after each.]
[Brief restrained light impact, about 0.4 seconds.] Kh!
[Sharper medium impact, about 0.65 seconds.] Ah!
[Fierce short heavy impact, about 0.9 seconds.] Hh-raah!
```

Treat that as an audition brief. Inspect for sung delivery, unintended words, accompaniment, multiple speakers and mismatched voice identity. If a clean source cannot be obtained, choose another performance route or preserve the incomplete slot explicitly.

## Edit from floating-point masters

- Keep the untouched full source, selected time windows, rejected/reserve takes and edit recipe. Time windows are handles; listen across both sides before committing a boundary.
- Downmix with `(L + R) / 2` after checking phase and stereo residue. Move an edge to a nearby zero crossing where it preserves the word/attack. Red used crossings within roughly ±3 ms, short anti-click fades, and high-quality sinc resampling.
- The reviewed delivery format is **mono 22,050 Hz, 16-bit PCM WAV**. Work at floating-point precision through edits and gain, then dither once at the final PCM conversion. Do not repeatedly normalize and redither already delivered files.
- Preserve attacks and the intended interruption behavior. The selected falling performance retained an open ending, with only a roughly 5 ms anti-click tail, because the game cuts it on landing.
- Assemble phrases deliberately when generation inserts long pauses. The two spoken lines used clean phrase cuts with approximately 0.30–0.35 seconds of inserted silence. Check the result against corpse impacts and other gameplay audio rather than only in isolation.

The project's most useful correction was phonetic. An initial “Again” crop ended before its central consonant; a second crop containing the back half was mistaken for a nonverbal cry. Andrei heard the word, and Red rechecked the full take and its consonant closure. The final bare word uses the complete 9.19–10.24 second phrase from that particular take. Those timestamps are not reusable for another recording. A waveform valley can be a voiced consonant closure, so neither silence detection nor a recognizer should replace listening to the whole phrase.

## Balance a severity ladder, not identical peaks

Compare short reactions using listening, peak/true peak and short-window energy. The initial common gain made the medium hurt less energetic than the light one despite its higher instantaneous peak. Lily therefore directed individual hurt gains, while preserving the approved death/fall family gain.

The retained review reported maximum 50 ms RMS near −14 dBFS for light, −13.3 dBFS for medium and −10.2 dBFS for the original heavy cry. Medium and heavy used a −1 dBTP ceiling; light needed no limiting. The medium did not reach its proposed −12 dBFS target without excessive extra limiting, so its attack was preserved. These are measured project choices, not universal loudness targets. In v1.2 the heavy trigger uses “Again”; the measured original heavy cry remains archived in the fourth row. Preserve this mapping distinction when comparing levels.

## Bind, exercise and version the bank

Give the character its own SND filename and register it with its model. Check dependencies, class/level sound budget and fallback behavior. A file existing in the archive does not establish that the caller reaches its row.

The final v1.2 death pool has nine rows: two spoken lines, “Again”, Cry A, and Cry B repeated five times. Repeated rows give Cry B five of nine choices if selection is uniform while reusing the same loaded WAV. Seventeen observed deaths cannot by themselves establish a probability law; use source/caller evidence for the selection rule and playback tests for reachability and audible behavior.

Exercise light/medium/heavy triggers across damage classes, rapid and spaced repeats, lethal hits, the full death pool, falling/landing, underwater events, pauses/save-load if relevant, and a second listener for multiplayer claims. Record actual engine build, renderer, sound backend, mod hashes and an in-game mix. Distinguish the recorded waveform from an agent's report about it.

The public [OpenJKDF2 voice-gate PR #519](https://github.com/shinyquagsire23/OpenJKDF2/pull/519) documents a priority-mask precedence defect that allowed overlapping voice sounds. Its reported comparison produced eight overlapping reactions on the affected build and two non-overlapping reactions with the correction, matching retail. The PR was open when checked on November 8, 2026; neither this chapter nor an asset revision proves the fix is present in a user's executable. Verify the actual target before relying on the gate.

## Retained artifact and completion boundary

The reviewed `Darth_Lilitari_and_Biddy_Blas_2026-11-04.zip` contains the R007 model with voice v1.2. Its `Darth_Lilitari_R007_Voice_v1_2.gob` has SHA-256 `7b5ee80da77cc303e3c26f33cca95643911d88f10981a21488e897caddef1633`. This historical package is separate from later animation/hand-fix archives with similar names.

Its README reports retail JK.EXE 1.01 software-renderer voice tests under Wine and OpenJKDF2 tests. This workflow update reviews those records and native assets; it does not rerun or newly certify gameplay. Different hand/model tests elsewhere must not be transferred to this sound-bank version.

**Five water-related voice slots remain stock placeholders in the reviewed package.** Lily marked them as incomplete for a fully original voice release. The delivered test package exists, but the complete custom voice bank was not finished. Keep that distinction visible instead of describing all ten generated batches as integrated sound coverage.

Credits: Andrei's direction and final listening/selection; Lily's performance direction, lines, prompts and revision judgments; Red's Suno operation, edits, conversion, SND integration and reported engine investigations. “Death… is only the beginning” was Andrei's supplied line; “The mask dies. I do not.” was Lily's contribution. Suno generated the source audio. Private conversation records and sound/game assets are not redistributed with this workflow.

See [runtime integration](runtime-and-packaging.md), [target-specific tests](testing-methods.md) and [the Tripo model route](tripo-to-jk.md) for the surrounding model contract.
