# Relay bay

Create a new editable RED campaign with `python make_project.py PATH-TO-NEW-DIRECTORY`. Open its `redlift.jkl` in RED. Project Cockpit can prepare its `red-project.json` and package the episode. The example contains two connected sectors, a gate, a rideable platform, and a switch on the striped wall above the platform.

For the gameplay scenario, use a compatible Linux OpenJKDF2 agent build with the normal JK controls and the game’s locally installed bootstrap resources. The player faces the gate at spawn. Hold Forward against the closed gate, press Use to raise it, walk to the striped wall, then press Use again while standing on the platform. The scenario runs the same route and captures the player at spawn, at the closed door, on the lift, and at its upper stop. The final assertion requires the player to remain in the second sector at a standing height of 0.56–0.58 world units for 350 ms.

The generator creates original indexed textures, a grayscale colormap, box meshes and a silent sound class. The project archive also contains the authored control script. The level uses a synthetic copyright section accepted by the compatible OpenJKDF2 build. All episode-specific assets come from the project mount. The game executable, base-game files, runtime configuration and captures stay outside the project.

## Edit and test the same room

Open the campaign in RED and select the first room’s floor. In Properties, change its material from `rlwall.mat` to `rlmark.mat`. To light the room, use **Place Sector Lights**, switch to **Light → Baked**, and choose **Recalculate Lighting**. In **Project Cockpit**, prepare the profile, review its resource winners, and test the **room-door-lift** scenario. The capture window returns to the open editor, where Undo remains available.

For the typed workflow, run the following commands from the generated project directory. Create an empty `edited` directory first. The two JSON recipes pin the authored resource archive, and the edit recipe also pins the source level.

```text
red-agent plan redlift.jkl edit-and-bake.json
red-agent apply redlift.jkl edit-and-bake.json edited/redlift.jkl
red-agent validate edited/redlift.jkl
red-agent package-plan edited/redlift.jkl reviewed-package.json
red-agent package-apply edited/redlift.jkl reviewed-package.json edited/redlift.gob <outputSha256> <manifestSha256>
```

Use both SHA-256 values from **package-plan**. The original room’s moving gate and lift use the regular vertex bake. Use the packaged-gameplay request with the reviewed archive and its digest to replay the four checkpoints.
