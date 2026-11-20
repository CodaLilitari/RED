# Relay sentry

This original JK/MotS fixture describes a stationary relay with a pivoting boom. The template binds its model, puppet, sound class and class COG. The model uses the indexed relay material, the puppet selects a short KEY, and the sound class plays a generated WAV. The two-geoset model and the single-geoset companion share the base and boom hierarchy.

Run `python generate.py` in this directory to reproduce the models, material and WAV. The template, KEY, PUP, SND and COG are authored text. None of the files contain retail art, audio or scripts.

The portable project tests generate candidate project and stock archives, then inspect the winning resources. They also remove the WAV to exercise a missing dependency and package a clean complete family for byte comparison.

## Reproduce the cross-reader checks

Build and run `sed_model_cross_reader_tests` in the RED build tree. The registered CTest is `sed_model_cross_reader`. To generate the inputs for Blender, run the same executable with `--emit <new-directory>`. It reads the checked-in originals, serializes the single-geoset 3DO with opaque, scalar 2.1 vertices, round-trips the KEY, and copies the two-geoset model, original MAT and PUP. It refuses an existing output directory. The output includes the byte-exact pre-conversion single-geoset model and KEY under `source/`, alongside `canonical-recipe.json`.

Run the maintained Blender-Sith add-on in a fresh background process with its legacy `sith` import available. Pass the emitted directory to the [Blender verification script](blender_readback.py). For a Blender executable, use `blender --background --python-exit-code 1 --python blender_readback.py -- <emitted-directory>`. A configured standalone Python environment with the matching bpy and add-on can run the script directly. The check constructs an original indexed palette in a temporary directory. Both strict reader and scene import must succeed. The strict reader checks every geoset, while the scene importer builds LOD 0. The PUP binds the stand action to the sparse KEY track for joint 1, `boom`.

The native corpus test also appends a synthetic `DESIGNER_NOTE` section. It checks that a lighting-only edit changes exactly 14 intensities across both geosets and preserves every other byte. A generic rewrite of either the two-geoset source or a one-geoset file carrying the extension must fail before writing bytes.

## Target and Blender observations

The [measured results](cross-reader-observations.json) include source/output hashes, the maintained Blender-Sith distribution hash, the engine and package for each scenario, the native camera request and framebuffer hashes. Blender 5.1.2 with Blender-Sith v1.2.0-coda.1 imported both models, bound the MAT on each mesh and created the boom action at 12 FPS. The strict KEY reader retained both markers and the native joint mapping.

In an original packaged room, an unmodified OpenJKDF2 capture build rendered the canonical single-geoset model. Inserting it changed 35,532 pixels relative to an empty placement. Its near view and the near view of the source-preserved two-geoset model were pixel-identical. Moving the level's LOD thresholds from `1 2 3 4` to `.01 .02 .03 .04` switched the same two-geoset asset to a visibly different far view (17,541 pixels). Both comparisons used the same requested camera.

Two held clips preserved the model's rest translation and isolated 0° versus 45° rotation of the boom. The images differed at 3,132 pixels. Separately, an observation-only engine build logged the actual local transform immediately before `rdMatrix_Build34` in `rdPuppet_BuildJointMatrices`, while the original KEY played. All 99 samples with track weight 1 had zero local translation, and the largest discrepancy from the authored yaw curve was 0.0000022°. The first sample had zero track weight and retained the rest translation. These observations qualify the recorded JK fixture, camera and engine builds. Software pose fitting of a hand, held object, camera-relative arm and planted foot is covered in RED's hierarchy tests.
