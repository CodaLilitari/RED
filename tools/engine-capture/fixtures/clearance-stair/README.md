# Clearance stair

Generate the editable project with `python -B make_project.py /tmp/clearance-stair`. The generator reuses the original Relay Bay mesh and palette helpers. Open the generated `redlift.jkl` or prepare its `red-project.json` with RED. Package the episode and run `scenario.json` against a compatible OpenJKDF2 agent with normal JK controls and locally installed game resources.

The player approaches a narrow model-faced door, pushes against it, opens it with Use, ascends shallow beveled native steps across seven sector boundaries, jumps, lands, descends and returns through the door. The contact floors and treads are convex native cells. Their reciprocal openings remain traversable at the transitions. The scenario checks actual sector, height and position with forward, backward and jump inputs.

In the observed run the closed gate stopped the player at y −.097, the upper tread supported the player at z .130, and a jump reached a sampled z .292. The return reached sector 0 at z .070. The [qualification record](../wind-gallery/qualification.json) identifies the exact engine and scenario archive. The measured heights apply to this authored profile and route.
