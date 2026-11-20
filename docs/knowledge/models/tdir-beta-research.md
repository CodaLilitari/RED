# TDIR's leaked beta: identity, roster evidence and recovery leads

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/tdir-beta-research.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Research checked 2026-09-30. Scope: character models, skins, animation and the provenance needed to compare builds. No game assets are included with this research.

**A leaked pre-release TDIR build is well attested, but its binary was not located and authenticated in this investigation.** The strongest identification is a pre-release of **Real DragonBall Z 4.0 / DragonBall Z: The Destruction Is Real**, remembered as the leaked or bootleg DBZ 4.0 and, in community discussion, **[pants]**. These are historical names and aliases, not a verified embedded version string or archive filename.

The research did recover the original project's early character, FAQ and download pages. They support a changed advertised roster, including SSJ4 characters, but cannot establish which files were in the leak. Keep that distinction when identifying a candidate or describing “extra beta skins.”

## Choose the useful next method

| Objective | Best available method | What it establishes |
|---|---|---|
| Learn TDIR's proven character pipeline now | Inspect the identified 1.1 package and the [existing TDIR model analysis](jke-tdir-shred-research.md) | Actual hierarchy, animation and material examples in an identified release |
| Recover the remembered additional characters | Follow the exact early names, image filenames and archival links below; authenticate any candidate before comparing | A route toward finding the build; the current page does not supply a beta download |
| Compare a newly found beta with 1.1 | Inventory ZIP/GOB contents, resolve selection tables and aliases, then compare model/texture/animation hashes and structures | Reproducible asset differences without running an unknown installer |
| Recreate an old visual idea | Make an original model from credited visual references, retaining a new provenance record | A new reconstruction; it must not be presented as a recovered historical asset |

## What the contemporary record supports

The early project's January 2001 FAQ describes the work as Real DBZ 4.0. Its files page labels **Real Dragonball Z 3.0** as the earlier version and separately links a **4.0 preview clip**. Thus “3.0,” “4.0 preview” and “TDIR leak” are not interchangeable labels. [Original FAQ, archived 2001-01-25](https://web.archive.org/web/20010125035700id_/http://www.jedinights.com:80/dbz/FAQ.html), [original files page, archived 2001-01-25](https://web.archive.org/web/20010125093600id_/http://www.jedinights.com:80/dbz/files.html).

A **2001-07-25** forum reply by ChewbaccaRC explicitly identifies DBZ 4.0 as DBZ: The Destruction Is Real while discussing a separate MotS project. This independently corroborates the early project alias; it does not establish that a beta had leaked by July 2001. [Contemporary naming clarification, post 16](https://forums.massassi.net/Editing_Forums/Cog_Forum/thread_13954_page_1.html#post190261).

By **2002-06-19**, XxCHAoSxX's Randsaber Beta 7 description referred to the leaked TDIR beta as a known lock-on comparison. That establishes circulation by this date, not the original leak date or an exact build number. [Massassi's Randsaber entry](https://www.massassi.net/levels/files/2211.shtml), [contemporary June 2002 news](https://www.massassi.net/news_archive_2002-6.html#news-item-2967).

On **2003-05-19**, a Massassi news item relayed Hell_Raiser's plan for one final internal team beta before packaging the public release. This later internal test is not automatically the earlier leak. [May 2003 announcement](https://www.massassi.net/news_archive_2003-5.html#news-item-3673).

Massassi dates the public TDIR release to **2003-06-27**. Its contemporary comments distinguish the earlier leak: From the Ashes calls it bootleg DBZ 4.0; tweaklink0072000 identifies the forum nickname [pants]. Sol reports different animation/power-up behavior and character-specific voices; tweaklink0072000 and bugarBrain miss disc/scattershot/Big Bang attacks. These are attributed player recollections, not a file-level comparison with 1.1. [Release page and dated comments](https://www.massassi.net/levels/files/2727.shtml).

## The extra-skin lead: advertised roster versus shipped assets

The project's **2001-02-24 archived character page** lists and links images for Goku (SSJ4), Vegeta (SSJ4), Super 13 and Super 17. Those are precise leads for recovering the remembered models. The page is evidence of the project's advertised characters at that time; neither a list entry nor an image link proves inclusion in the leaked package. The linked images were not fetched or visually assessed in this investigation. [Original character page](https://web.archive.org/web/20010224000716id_/http://www.jedinights.com:80/dbz/char.html).

| Early advertised character | Image filename linked by the original page | Bounded comparison with 1.1 |
|---|---|---|
| Goku (SSJ4) | `goku4.jpg` | Later FAQ explicitly excludes SSJ4 transformations; no SSJ4 entry was identified in the inspected selection table |
| Vegeta (SSJ4) | `vegeta4.jpg` | Same limitation and later exclusion |
| Super 13 | `s13.jpg` | No Super 13 entry identified in the inspected selection table; no similarly named model found in the inspected character archive |
| Super 17 | `s17.jpg` | No Super 17 entry identified there; ordinary Android 17 is present and must not be mistaken for this variant |

The later FAQ reproduced by ModDB excludes Super Saiyajin 4 and Oozaru transformations, and describes generic voices. Its late-project scope differs from the early advertised roster. ModDB is a **republication by qwerty_/NOXIOUSVEGETA**, who explicitly disclaims authorship; credit the original TDIR team for the work. [ModDB's preserved description and FAQ](https://www.moddb.com/mods/the-destruction-is-real).

**Do not add King Kai, King Vegeta, Mr. Popo or Tapion to a “beta-only” list.** The inspected 1.1 `tdircharacter.gob` contains `kingkai.3do`, `kingvegeta.3do`, `popo.3do` and `tapion.3do`; its control archive's `misc/models.dat` lists them too. The character archive contains 97 `.3do` files, which is not a count of distinct selectable characters. Transformations, variants and selection logic require their own accounting. See the [1.1 package analysis](jke-tdir-shred-research.md) and [machine-readable source ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/tdir-beta-sources.json).

The `models.dat` inspected here declares 87 entries but has 86 numbered rows (0–85). Report the observed rows and the declaration separately. This static observation is not a runtime roster test, and the custom launcher/character logic may add another selection layer.

Likewise, the 1.1 control archive contains `cog/ability_disk.cog`. Its presence alone does not establish a playable disc attack, but it prevents turning the 2003 recollections into the claim that 1.1 contains no disc-related code. No new game execution or multiplayer test was performed for this beta investigation.

## Downloads and candidates checked

| Candidate or route | Result on this investigation |
|---|---|
| [Massassi TDIR release](https://www.massassi.net/levels/files/2727.shtml) / `TDiR.zip` | Identified June 2003 public release. Existing local ZIP inspected: `TDIR.EXE`, installation instructions and release readme. This is not the sought pre-release beta. The installer was not run |
| [ModDB 1.1 listing](https://www.moddb.com/mods/the-destruction-is-real/downloads/the-destruction-is-real-111) | Identified later 1.1 comparison package. The public listing was readable through web retrieval; direct anonymous HTTP requests to ModDB were denied with 403 during the final check |
| [JKDF2 public `jkmod` archive](https://www.jkdf2.net/JKBot/JKArchive/?dir=JKDF2/JKDF2/Game%20Files/jkmod) | Listing includes the public `TDiR.zip`, `dbz.zip` and `dbzsaiyan20.zip`; no authenticated TDIR beta identified in the bounded directory search |
| `dbz.zip` from that archive | Downloaded privately and read as ZIP data. Readme identifies **DBZ Fighter v1.0 beta**, by IA_D_Vadergator / MG_Dash_Rendar. Different project; excluded |
| Local `tdir-cicdec.zip` and `tdir-cicdec-source.zip` | Installer-extraction utility `cicdec` and its C# source, respectively. The local filename prefix describes why they were acquired; neither contains a TDIR release or TDIR source tree |
| Early project `dbz30.zip` | Exact link recovered from the archived files page; labeled predecessor 3.0. Binary was not recovered or authenticated |
| Early project `video.zip` | Exact link recovered; labeled 4.0 preview clip. A preview is not a mod archive; binary not recovered |
| Internet Archive catalog and selected Wayback indexes | No authenticated beta binary found. Three original project pages were successfully replayed with HTTP 200. A later CDX batch returned 429; archive querying stopped rather than treating the limit as evidence of absence |

The [current ModDB file listing](https://www.moddb.com/mods/the-destruction-is-real/downloads) exposes the 1.1 release. Its statistics or historical references to other downloads do not establish that another listed file is the missing beta.

The early download references resolve against `http://www.jedinights.com/dbz/` as `dbz30.zip` and `video.zip`. Preserve those filenames as recovery leads, **not as verified working download links**. Historical project routes also include `tdir.hellraiser64.com` and `www.hellraiser64.com/tdir/`, identified by contemporary Massassi news. No additional mirror or guessed filename is asserted here.

## Additional downloaded-corpus evidence

The [broader package inspection](downloaded-corpus.md) confirms that DBZ Saiyan 2.0 is BeowulftheBard’s separately identified project and rechecks the different DBZ Fighter beta. A retained readme from Atom’s Hyperbolic Time Chamber uses TDIR and DBZ 4.0 together, adding contemporary alias evidence without recovering the missing beta. The broad inventory has explicit installer exclusions, so its filename search must not be presented as proof that no beta exists in any inaccessible or unexpanded material.

## How to authenticate a future candidate

1. Preserve the source URL, acquisition date, byte count, SHA-256 and original filename. Record whether the provider claims to be an author, mirror or later collector.
2. List archive entries as data. Read the release readme, patch information and embedded build identifiers before extracting models. Do not infer a version solely from the outer ZIP name or filesystem timestamps.
3. Inventory `3DO`, `MAT`, `KEY`, `PUP`, model-selection tables and the character-selection script. Compare all relevant GOBs: a small `character.gob` can contain selection logic while `tdircharacter.gob` contains geometry.
4. Resolve aliases and transformations before declaring a character missing. Compare bytes, geometry, materials and hierarchy independently; renamed files, altered skins and different models are different kinds of change.
5. For these leads, first test SSJ4 Goku/Vegeta and Super 13/17. Then compare animation allocation, model sizes and the attacks/sounds identified in testimony. Keep early plans, archive contents and playable behavior in separate columns.
6. Render or test only after the static identification is complete and the intended engine/renderer is recorded. Apply the [compatibility checks](compatibility.md) and [testing methods](testing-methods.md) before claiming retail, MotS or connected multiplayer behavior.

This method makes a later discovery easy to integrate without rewriting a rumor as fact. The present result is **a documented identity and roster lead, with an unresolved binary search**.

## Credit and evidence boundaries

Original project: Shred18 and the TDIR team. The inspected 1.1 package's `docs/Character_Readme.txt`, dated May 2003, credits Shred18 and Gsusnme for character work, and Jenny Kitty for conversion to the new animation system. This is distinct from the older November 2001 readme embedded in `tdircharacter.gob`. Massassi preserves contemporaneous announcements and participant comments; the Internet Archive preserves the early project pages; JKDF2 and ModDB provide later preservation routes. Individual observations above remain attributed to their speakers.

This document and its JSON ledger contain original synthesis, short factual metadata and limited quotations. They do not grant redistribution rights to the mod, beta, character art, audio or screenshots. No third-party mod binaries, model files, full forum transcripts or original artwork are included in the workflow release.
