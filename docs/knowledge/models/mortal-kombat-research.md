# Historical Mortal Kombat skins for Jedi Knight

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/mortal-kombat-research.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Checked September 30, 2026. This is a bounded recovery investigation for Jedi Knight: Dark Forces II character models. It found three downloadable JK-era packs and an older missing-pack lead. **Cyrax is advertised by one recovered release, but a standalone working Cyrax model has not been established. Sektor remains unlocated for JK/MotS.** No game or historical executable was run in this investigation.

## Recovered packages

| Package | Author and date | What the actual download establishes |
| --- | --- | --- |
| [Mortal Kombat Skins SE](https://www.massassi.net/levels/files/2477.shtml) | Mr. X; Massassi listing November 11, 2002 | The author explicitly lists Cyrax among eleven characters. The recovered `mkskinsse.zip` has a readme, two BMP screenshots, and a GOB with 188 MATs, one CMP, one COG and `patchinfo.txt`. **There are no 3DO files or model-selection table in that GOB.** The reused patch metadata still calls itself “Mortal Kombat Skins 1.” The readme claims new 3DOs, while the recovered GOB supplies none; its advertised roster remains unverified in a working installation. |
| [Mortal Kombat Skins 1](https://www.massassi.net/levels/files/2471.shtml) | Mr. X; Massassi listing October 29, 2002 | The ZIP includes a skin GOB, a separate level GOB, screenshots, readme and loose fist COG. The skin GOB has 114 MATs, a CMP, a COG and patch metadata; no 3DO or model-selection table. The listing and readme give different rosters. The level is outside this workflow's modeling scope. |
| [Mortal Kombat 2 Skinpack](https://www.massassi.net/levels/files/2797.shtml) | Darth Slaw / Colin Hurley; listed October 21, 2003; readme says completed January 25 and released September 11, 2003 | `mk2sp.zip` contains a readme and GOB with 14 actual character 3DOs, 90 MATs, `models.dat`, `jkstrings.uni`, and patch metadata. The 42-row selection table appends 14 fighters to the 28 stock choices. This is a concrete model-and-texture recovery, though runtime compatibility remains untested. It has human Smoke, not cyber Smoke, and neither Cyrax nor Sektor. |

A texture-only skin pack can legitimately reuse stock geometry without shipping a new 3DO or selection table. Mr. X's MAT names here use custom prefixes such as `DaBraz` and `DRBraz`; their matching model references were not established in this investigation. This is an unresolved integration question, not proof that every texture-only skin pack is incomplete.

Darth Slaw's recovered models are Baraka, Johnny Cage, Jax, Liu Kang, Kitana, Kung Lao, Mileena, Rayden, Reptile, Noob Saibot, Scorpion, human Smoke, Sub-Zero and Shang Tsung. All 14 files declare one geoset, 16 meshes and 18 hierarchy nodes. These are static header measurements, not animation or retail-playability results. His readme names JED, JOKE, ConMan, MatMaster, Paint and PerfectPhoto; it reports a shading issue on some characters and explicitly treats the colormap explanation as a guess.

Mr. X credits Discman 13 and lists Discman 13 and Monkey torp as additional testers. Preserve those contributions when discussing the packs. Neither the publication dates nor the archive's filesystem timestamps replace the authors' own development dates.

## Original downloads and independent mirror checks

Each original Massassi download was fetched and compared with the independently hosted JKDF2 archive copy. All three pairs have identical SHA-256 hashes:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| [mkskinsse.zip](https://www.massassi.net/media/levels/files/jkmod/mkskinsse.zip) | 154,195 | `22ca8d32dce2c34936731d60fb9475690150f4f2acc072c736e83f09567c68a5` |
| [mkskins1.zip](https://www.massassi.net/media/levels/files/jkmod/mkskins1.zip) | 457,668 | `f828c7d52fdf574ddf8157fbeb5272e24b8f3ebedb6618ac8aeca371ba2b8afb` |
| [mk2sp.zip](https://www.massassi.net/media/levels/files/jkmod/mk2sp.zip) | 265,394 | `60e4e930151a256daac803ccb14fa35c3687e693b33e2ff4412eaf807d8b4716` |

Independent copies: [SE at JKDF2](https://www.jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/jkmod/mkskinsse.zip), [Skins 1 at JKDF2](https://www.jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/jkmod/mkskins1.zip), and [MK2 at JKDF2](https://www.jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/jkmod/mk2sp.zip). These are verified live mirrors, not dated Wayback snapshots. The research package preserves original analysis, identifiers and hashes; it does not redistribute the third-party artwork or raw archive contents.

## Older lead and exclusions

Brian's [October 18, 1999 news entry](https://www.massassi.net/oldnews/old33.shtml) announces a Mortal Kombat skin pack on the hosted **Sector: JK Co-Op** site's skins page. The announcement preserves the original `http://www.massassi.net/sector/skins.shtml` link. No package, ZIP filename, author, roster or verified archived snapshot was recovered for that older pack in this pass. **Sector is the site name; it is not evidence that Sektor was included.** Keep this as a specific recovery lead rather than assigning it to the later Mr. X or Darth Slaw packages.

Two plausible archive names were examined and excluded: `KOMBATKY.ZIP` is BILLMAN's October 1997 Combat Suit Kyle, and KnightSpider's January 2000 `Cyborg skin pack.zip` credits Al MacDonald's Dark Trooper/Future Cop materials. Neither package provides evidence of Mortal Kombat's cyborgs. Searches also return later Jedi Academy/Outcast MK packs; those use another engine and are not recovery of the requested JK/MotS models.

## Additional community lead

In a **June 7, 2022** Massassi Discord search result, **KnightCop** says a requested Reiko from Mortal Kombat 4 was completed using a mixture of original and JK textures, with the small loincloth omitted. This is an attributed authoring report, not a recovered archive or inspected model. The message's own header and body were read in the search result; wider context and attachments were not opened successfully. [KnightCop's Reiko report](https://discord.com/channels/881034839401328680/881044857328652328/983752217225986118).

The accompanying member-visible, server-wide search returned zero results for `Sektor`, zero for `Cyrax`, and nine for the exact phrase `mortal kombat`; all nine phrase results were read. These bounded queries do not establish absence from older, unavailable or differently named material. The [source ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/mortal-kombat-sources.json) preserves the query limits alongside the package evidence.

## Wider downloaded-package check

The later [all-package inventory](downloaded-corpus.md) found a `sektor.mat` filename in Calling the Corners — Sabers Version. Its inspected models and readme do not establish a Sektor player skin, and its 3DO material tables do not reference that MAT. The file-name hit is preserved as a bounded lead; it does not change the recovery conclusion above.

## Workflow lessons

- Offer a reskin on a known working hierarchy when standard movement is sufficient; keep custom animation as a separately costed choice.
- Inspect the actual package, resource names and selection entries before promising a particular character. A readme roster and screenshots do not establish complete integration.
- Keep dates, author aliases and dependency claims separate. Determine whether the textures intentionally replace stock materials or need other model references; absent 3DOs alone do not prove an incomplete release. Do not invent a dependency or mark a package repaired without testing.
- For an eventual Cyrax/Sektor recovery, retain the original archive and readme privately, hash it, identify the 3DO-to-MAT mapping, and test the intended engine and multiplayer selection path before calling it playable.
