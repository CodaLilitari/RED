# Rebuild the Nar Shaddaa JK16 glass repair

This recipe transforms one specific 16-bit Jedi Knight episode archive into its reviewed glass variant. Supply your own copy of the Nar Shaddaa Streets parent. The recipe includes the generator, resource identities and expected hashes. Game resources come from your archive.

| Artifact | SHA-256 |
|---|---|
| Required parent GOB | `bba754108bc0986e2aa4aa97f534aca5ca9a7c5d2597120e73a410c2cc5328f8` |
| Rebuilt GOB | `0ed7c70a169a928cb3b6f9dd0dd5cf35c851e95f89f0ff362393e22274ab9dff` |

From the repository root, use Python 3 to generate the 16 loose resources for inspection. The destination's parent directory must exist. Choose a fresh output on a filesystem that supports hard links.

```sh
python -B tools/engine-capture/recipes/nar_glass/build.py \
  --parent /path/to/parent/nsstreets.gob --overlay /path/to/new-overlay
```

To produce the exact playable GOB, point the recipe at a local RED agent executable with the `validate` and `archive-create` commands. Allow about 110 MiB free beside the output. Staging is temporary. Keep the original separately and choose an unused destination.

```sh
python -B tools/engine-capture/recipes/nar_glass/build.py \
  --parent /path/to/parent/nsstreets.gob --archive /path/to/new/nsstreets.gob \
  --red /path/to/red-agent
```

The builder checks the parent SHA-256 before parsing the GOB. It checks all generated resource hashes against [recipe.json](recipe.json), verifies the parent again after staging, and refuses existing destinations. Archive mode runs strict JKL validation and verifies the resulting archive hash before publication. The final GOB has 597 members. Of these, 581 retain their original payloads, nine have new payloads, and seven are new visual models.

## What changes

- The generated `ns67.mat` is an unkeyed, four-mip RGB565 material. The broad pane uses a dark blue-grey tint. Local highlights follow the exposed sides already identified by the parent's UVs. The internal triangle diagonals retain the base color.
- Seven fixed, noncolliding mixed models donate their visible glass triangles to new visual Things. The split keeps the geometry, corner lighting, texture coordinates, normals and winding. Existing Thing IDs remain stable. The 36 breakable Things keep their collision models and COG connections.
- The revised placement list submits the glass later. A placement override also moves 131 verified noncolliding architecture Things into the sector pass, which flushes before the ordinary Thing pass. List ordering alone allowed the renderer to regroup the panes ahead of nearby buildings in a shared batch.

The exact parent identifies the 131 eligible placements and the seven identity-transform donors. Other releases, maps, moving doors and different UV charts require independent review. The material changes the color of the pane. The engine's blend and depth behavior still determine how much detail passes through it.

## Checks

```sh
python -B -m unittest discover -s tools/engine-capture/tests -p test_nar_glass_recipe.py -v
```

Compare matched cameras from both sides of a breakable pane, under the fixed roof with buildings and stars behind it, and in a glass-free part of the city. The approved visual evidence came from OpenJKDF2. Original retail rendering, multiplayer and other color editions require their own checks. See the [engine capture guide](../../README.md).
