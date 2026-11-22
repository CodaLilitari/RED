# Rebuild the Bespin glass overlay

This source-only recipe generates one loose JKL for one of four Bespin conversion archives. Supply the matching Jedi Knight (JK) GOB or Mysteries of the Sith (MotS) GOO. The recipe identifies the edition by its SHA-256 before reading the archive directory.

| Parent edition | Required archive SHA-256 |
|---|---|
| JK 16-bit | `707e3b32ae6030f93cc27e3528ad4b1df7ddf06e0f788bf40eaee9ce93ea26a3` |
| JK indexed | `861352f45723e8655b0b5af6232d3fa9468a3c6cb3cc09fde6ac2cef27c6dc6d` |
| MotS 16-bit | `30c898ccb176c58a01120f6675e123ec633088d9e6cbf7cb563a080aa2fa2632` |
| MotS indexed | `d981b1057d5ac477fe721e2b7fe75fd7288868430bcb597f794d0fc7dc8f91db` |

Run from the RED repository root using Python 3. Choose a fresh output directory whose parent exists on a filesystem with hard-link support.

    python -B tools/engine-capture/recipes/bespin_glass/build.py --parent /path/to/bespin_c02.gob --overlay /path/to/new-overlay

The output is `new-overlay/jkl/bespin.jkl`. Its SHA-256 is `21851f855d899f7c50e18b2375edd74afc77f8fc3424bcb494346bfac1c52b1f` in both JK editions and `81a6b253c3aed565bf2c9ac1d60dbf544c291819c37859d3c16d05466b0bddea` in both MotS editions. The builder checks the staged output digest and rechecks the parent before publishing. Archive assembly is outside this recipe.

## Why the editions differ

A matched native camera showed the solid room through a deliberately hidden window. The intact glass suppressed that room. Both window Things, IDs 269 and 270, inherit LEVELGEO from the structure template. Clearing that bit moves their existing glass faces to the ordinary Thing pass. The 219 opaque architecture Things already use LEVELGEO on MotS. On JK, the recipe also adds that bit at the 219 original placements, so the accelerated renderer finishes the opaque room before submitting the windows. The original MATs supply the tint.

The builder resolves inherited and placed properties before and after the edit. Only the two panes on MotS and those panes plus the 219 static architecture Things on JK change effective draw flags. The JK output differs from its parent JKL at 220 bytes and the MotS output at one. Every change is a single hexadecimal digit. Thing IDs, model assignments, collision, material and COG bindings are preserved.

This is a candidate for the accelerated OpenJKDF2 renderer. The source JK conversion used the ordinary Thing pass for architecture to address software-renderer depth ordering while retaining distance shading. The candidate preserves that shading flag and changes the pass. Software rendering and the original retail executables still require independent tests, particularly at distant or overlapping architecture.

See the [native capture guide](../../README.md) for matched camera and gameplay checks.
