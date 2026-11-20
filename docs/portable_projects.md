# Portable campaign projects

Place `red-project.json` next to the campaign's `episode.jk`. Open **Tools > Project Cockpit…**, select
the profile and your local stock root, and choose **Prepare**. The tree lists the target, campaign
levels, resource winners, shadowed archives and scenario references. **Create Package…** writes a new
JK GOB or MotS GOO after checking the reviewed inputs again. **Campaign Editor (F8)** opens RED's
existing progression and goals editor when the open level belongs to this campaign.

For a two-level JK campaign the profile has this shape. Replace each digest with the lower-case SHA-256
of that archive. Use `Get-FileHash` on Windows or `sha256sum` on Linux.

```json
{
  "schemaVersion": 1,
  "target": {"game": "jk", "engine": "OpenJKDF2", "renderer": "OpenGL"},
  "episode": "episode.jk",
  "levels": [
    {"native": "source/arrival.sed", "export": "jkl/arrival.jkl"},
    {"native": "source/vault.sed", "export": "jkl/vault.jkl"}
  ],
  "mounts": [
    {"kind": "project", "path": "art/custom.gob", "sha256": "replace-with-custom-archive-sha256"},
    {"kind": "stock", "path": "Resource/Res1hi.gob", "sha256": "replace-with-stock-archive-sha256"}
  ],
  "files": [{"path": "misc/cogstrings.uni", "destination": "misc/cogstrings.uni"}],
  "scenarios": [
    {"name": "Door and lift", "level": "arrival.jkl", "definition": "scenarios/door-and-lift.json", "receipts": []}
  ]
}
```

Project paths are relative to the profile directory. Stock paths are relative to the installation
root supplied on this machine. That root stays out of the shared record. The optional `native` field
identifies the authoring document. Package preparation reads the exported JKL files. Every playable
`LEVEL` row in the existing episode must have a corresponding export, and campaign branches must lead
to valid rows. The build retains the original episode bytes, including its progression.

The one-level package planner supplies each level's dependency graph. Higher-priority mounts come
first within a lookup tier. Required stock resources are omitted from the package. Shared custom
files with matching destinations and identical contents are included once. Conflicting destinations
refuse publication. Declare additional campaign assets, including string tables, through `files`.
When a mounted archive changes, review it and update its recorded digest before preparing again.

## Finding and inserting a resource family

In **Project resources**, choose **Inspect resources**, then search across the declared archives and
level templates. Selecting a result shows its winning source, shadowed copies, the resources it
uses and the authored objects that would be affected by replacement. **Preview** opens the typed
preview against the captured project archives and game profile. **Inspect LODs** shows every
declared geoset, hierarchy pivots, materials and radius. Its JK loader limits and retail drawing
observations have separate labels. The engine and renderer named in the profile still require a
runtime test.

To insert an actor, open the campaign's declared native document or JKL, select one unlocked
sector and select the actor's template in this browser. **Insert family** places it at the
sector center. Review the supplied project assets and resources required from the game.
Identical active files can be reused. Choose a different template name to keep an unrelated
local definition. Resolve differing parent definitions and other conflicting file names at the
source, remap their references, then inspect again. A missing input prevents insertion.

RED reacquires the content revisions before applying. It creates missing project files through
the gameplay publication journal and binds the local templates and Thing in a single Undo entry.
Undo removes that placement and its added definitions. The files remain reusable across levels.
Save and export the changed level before preparing a new campaign package. The family walker
covers static resource references, including COG symbol defaults. Resources chosen dynamically
by script need explicit project declarations and a runtime check.

A scenario definition is a project-relative JSON object describing the route or camera. Receipts are
project-relative files associated with that scenario. Preparation reports the target as untested.
A successful package build by itself says nothing about engine launch, playability or performance.
