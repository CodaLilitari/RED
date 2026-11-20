# Relay bay

Create a new editable RED campaign with `python make_project.py PATH-TO-NEW-DIRECTORY`. Open its `redlift.jkl` in RED. Project Cockpit can prepare its `red-project.json` and package the episode. The example contains two connected sectors, a gate, a rideable platform, and a switch on the striped wall above the platform.

For the gameplay scenario, use a compatible Linux OpenJKDF2 agent build with the normal JK controls and the game’s locally installed bootstrap resources. The player faces the gate at spawn. Hold Forward against the closed gate, press Use to raise it, walk to the striped wall, then press Use again while standing on the platform. The scenario runs the same route and captures the player at spawn, at the closed door, on the lift, and at its upper stop. The final assertion requires the player to remain in the second sector at a standing height of 0.56–0.58 world units for 350 ms.

The generator creates original indexed textures, a grayscale colormap, box meshes and a silent sound class. The project archive also contains the authored control script. The level uses a synthetic copyright section accepted by the compatible OpenJKDF2 build. All episode-specific assets come from the project mount. The game executable, base-game files, runtime configuration and captures stay outside the project.
