# Two-client game scenario

The included recording contains numerical observations from two independent Linux OpenJKDF2 processes. The clients used GameNetworkingSockets over a private loopback interface, an installed JK resource set, two private Xvfb displays, Mesa 26.0.8 llvmpipe, 640 × 480 and swap interval zero. The 31 KiB ZIP contains the observations, exact deployment and package identities, native pre-swap timestamps, contacts and action marks. It contains no game resources, engine or rendered frames.

To replay the evidence, extract the supplied archive into a new local directory and run the packaged importer.

```sh
python3 -m zipfile -e observed-loopback.zip observed
python3 -B ../../peer_qualification.py observed/profile.json
```

The importer returns 0 when every declared check passes, 1 for an observed failed assertion and 2 for invalid or mismatched evidence. It can also bind the next pair of host and guest directories to the configured bounds and budgets.

```sh
python3 -B ../../peer_qualification.py scenario.json --bind fresh-observations > fresh-observations/profile.json
python3 -B ../../peer_qualification.py fresh-observations/profile.json
```

## What the native target did

OpenJKDF2-MotS-Fixes was built from `f554833fd008eee0ba9c1cd09918a815441b56d8` with an external read-only observer and a local menu tracer for navigation. The engine SHA-256 was `d19a87dfa618d500132b23c17869e658b5331e230c087f9e5650b2c014479cdb`. Both installations accepted archive `512fb95bc80e88b919713c0a9e447d89a8235f2a174af3d672ab6a9d6c48dbd8` before and after play.

| Observation | Paired result |
| --- | --- |
| Late join and spawns | Host played alone for 11.174 seconds before the guest joined. Both views placed the two actors at distinct authored starts in sector 34 with 100 health. |
| Movement and weapons | Input from each participant moved the same actor on both clients. Each participant’s ordinary Bryar fire produced native `+bryarbolt` contacts with the opposing actor on both endpoints. |
| Items and lift | Both views observed disabled pickups return and reported the moving stock lift from about −0.99 to +0.01 JKU. This fixture observes lift movement at distance. |
| Death and respawn | Both clients counted the guest death. The guest recovered local health and its post-respawn movement was reproduced on the host. A nonowning client can retain a stale health value. |
| Thrown object | The guest selected weapon 4 and threw a thermal detonator. Both native surface contact handlers recorded `+grenade1` in sector 34 against surface 307. |
| Broken synchronization | The lab disconnected its private loopback link. The host traveled 0.44229 JKU. The guest measured zero displacement, so the same movement assertion that passed before disconnection failed. |
| Capacity, visibility, time | Both loaded 347 Thing slots. Peak occupancy was 104 on host and 105 on guest; minimum free slots were 243 and 242. Each observed up to 10 projectiles, 12 visible sectors and 12 frustums. Native p95 frame intervals were 29.65 and 29.05 ms, within this fixture’s 50 ms software-renderer budget. |

This qualifies the two-player JK fixture, including a thrown inventory weapon. MotS, Force Throw, high-occupancy games, hardware graphics performance, physical networks and every room’s sightlines have separate evidence requirements.

## Record another run

1. On a machine with the author’s original Carbon Crown Exchange build, `make_carbon_crown_scenario.py ORIGINAL.gob OUT.gob` generates the exact measured variant in a new file. It creates two legal opposing starts, uses inert markers for other original starts, and places shields and detonators on the ordinary routes. It verifies the original and output SHA-256. Keep both GOBs private; the original contains installed game resources. For another authored map, adapt the start volumes, tracked Thing indices, accepted thrown template names and thresholds.
2. Use two separate game directories, profiles and displays. Verify and record the exact archives, engines and initial configurations. For this loopback scenario the host used ordinary `-autostart -episode redarena -map carbon_crown.jkl -windowGUI`. The guest opened **Multiplayer → Join Game**, chose the loopback provider and existing session, selected a rank-zero character and joined after the host entered the level. Both used normal physics and ordinary input. The optional `openjkdf2-peer-observer.patch` records read-only game state in each working directory when `RED_PEER_OBSERVE=1` is set. The patch’s pickup indices 13 and 31 and lift index 90 belong to this fixture. Build it from the named source with agent hooks enabled. Launch the multiplayer clients normally without the single-player `-agent` switch.
3. Journal each action with its endpoint and the last native observation timestamp from each process. Both players first walk forward separately and fire at the opposite actor. Host sustained fire kills the guest. The guest uses ordinary fire to respawn, walks sideways, selects and throws the thermal. For the deliberate failure, disconnect **only the isolated test link** and walk on the host while the guest continues to observe. The names and ordering required by this named scenario appear in the recorded `actions.jsonl`. Each SDL clock is local to its process.
4. Store `deployment.json`, `package-sha256.txt`, `peer-target.txt`, `peer-frames.csv`, `peer-contacts.jsonl` and `peer-observation.jsonl` in separate `host/` and `guest/` directories. The final package digest comes from a fresh read of each staged GOB. Use the recorded deployment schema and the proper transport class for the session. Bind and verify the result with the commands above. The CLI never starts a client, drives a desktop or alters networking.
