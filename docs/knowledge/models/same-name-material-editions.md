# Same-name material editions for original Jedi Knight

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/same-name-material-editions.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

An indexed edition and an accelerated RGB565 edition can retain the same internal character identity while supplying different local MAT payloads. A concrete historical example is TIE's **Guardians of the Galaxy — Master Edition**, released as separate [16-bit](https://www.massassi.net/levels/files/1864.shtml) and [8-bit](https://www.massassi.net/levels/files/1865.shtml) packages. This is a useful alternative to creating a second selectable character merely to choose a texture format.

The edition is chosen by the local installation. This finding does **not** establish automatic renderer detection inside one GOB, or guarantee multiplayer compatibility from a shared filename alone. Keep gameplay and network-relevant resources compatible, and test the exact editions together.

## Historical testimony and measured archive facts

On January 6, 2002, at 12:08 p.m., TIE answered a question about 16-bit and 8-bit users playing together: **“Yes, they're compatible.”** That is the author's contemporary compatibility statement, not a newly executed test by this workflow. The same page identifies the accelerated edition's video-card requirement. [Original release and discussion](https://www.massassi.net/levels/files/1864.shtml).

A fresh passive comparison of the two identified archives found:

| Shared resource | Byte comparison |
| --- | --- |
| 171 native 3DO models | All identical, including material-reference names |
| 27 COG scripts, 91 KEY animations and 8 PUP files | All identical |
| Three DAT files, including the 30-row model selector | All identical |
| Static JKL, display strings, sound classes and audio | All identical |
| 493 MAT paths | 200 differ as 16-bit versus 8-bit; 104 differ while both are indexed; 189 are identical indexed MATs |

For example, both selector tables choose `darth.3do` with `vader.snd`. Both copies of that model reference `dvchest.mat`; the same `mat/dvchest.mat` resource is 16-bit in one local edition and 8-bit in the other. The outer GOB filenames differ, so matching the archive filename was not necessary for this shared internal model identity.

The complete GOBs are not a MAT-only difference: eight bitmap payloads and one text payload also differ, and the accelerated edition contains nine additional MATs. The [portable evidence ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/same-name-material-editions.json) records the exact ZIP/GOB hashes, counts, example hashes and limits. It contains no historical asset payloads.

## Why the local-edition explanation fits

The inspected original JK 1.01 player-model route sends a model filename and loads that filename from the receiving installation. A pinned reconstruction shows the same operation: the sender writes the selected model filename, and the receiver calls the local model loader before assigning the model. This supports different local materials behind the same model identity; it is not a claim that textures are negotiated or transferred between peers. [Pinned sender and receiver source](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/3c6814b724871f7e1fe33d12d2e20ec93499c0b3/src/Dss/jkDSS.c#L378).

The inspected original material route and its pinned reconstruction use a fixed world lookup order and a MAT-wide pixel format. They do not establish a supported display-mode fallback from a successfully loaded RGB565 MAT to a second indexed payload. Likewise, different menu and world lookup orders can serve their different renderers, but that distinction alone does not select software versus accelerated **world** materials. These are bounded findings about the inspected paths, not proof that every possible script or modified engine has been ruled out. [World material loader](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/3c6814b724871f7e1fe33d12d2e20ec93499c0b3/src/World/sithMaterial.c#L141), [native MAT reader](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/3c6814b724871f7e1fe33d12d2e20ec93499c0b3/src/Engine/rdMaterial.c#L185).

## Reusable packaging method

1. Preserve the same internal 3DO names, geometry, hierarchy and material-reference names in both editions. Preserve the intended selector, animations, puppets, scripts, sound bindings and static-resource relationships.
2. Derive the indexed and RGB565 MATs from the same accepted artwork. Preserve dimensions, cells, mip grids and transparency semantics; inspect both decoded textures and mapped game appearance.
3. Make the edition delta explicit. For a new package, a MAT-only delta is easier to audit than an unexplained collection of script, model and registry changes.
4. Install exactly one edition's overlapping resources per local installation. Label the downloads clearly, describe precedence and old-copy removal, and restart before switching. Installing both does not make them an automatic fallback pair.
5. Treat indexed MATs as a broader compatibility option: they can also be used with acceleration. They remain palette-dependent; an identical base RGB match does not guarantee identical shading under every CMP or custom map.

Matching internal names is necessary for this design but insufficient to certify it. Different gameplay scripts, registration, static resources or model/animation contracts can still cause incompatible behavior. A peer missing the package is a separate case from a peer using its alternate texture edition.

## Checks before claiming compatibility

Verify exact resource-provider precedence and the intended graphics setting on each client. Test a software client with the indexed edition against an accelerated client with the RGB565 edition, in both host directions. Observe the other player's actual model, movement and materials; also test late join, respawn and reselection. A successful host start, a checksum calculation, a local actor or matching archive metadata is not a substitute for those observations. Repeat separately for a client with no package if that compatibility is advertised.

This chapter adds historical artifact evidence and a source-supported design option. It does not report a new connected-multiplayer pass for a current character. Original JK, original MotS, wrappers and source ports remain separate targets.

[JkGfxMod](https://github.com/jdmclark/jkgfxmod) is a distinct modern route with its own material-pack system and installation contract. Its advanced replacement materials should not be confused with native RGB565 MATs or cited as evidence of an automatic original-engine fallback.

## Attribution

**TIE** authored the historical Guardians releases and the compatibility statement; **The Massassi Temple** preserves the releases and discussion. **Andrei** proposed the same-internal-name, separate-local-edition explanation that prompted this focused comparison. **Coda**, collaborating with Andrei, performed the source synthesis and independent archive checks. The design is historical precedent recovered and checked here, not a claim of inventing the technique.

**Lilitari (GPT-5.6 Pro)** independently reviewed the material-loading and multiplayer-identity distinctions and proposed falsifiable native tests. The advisory informs the checks above; it does not substitute for running them.
