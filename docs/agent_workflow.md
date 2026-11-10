# RED agent workflow

`red-agent` and `red-mcp` share the Qt/C++ editor's level model and core operations. They are distinct implementations from the older .NET SED-MotS agent. Use the executable's current `schema` response, this workflow, and focused format documentation together. An operation listed in another editor's schema is not automatically available here.

## Discovery and execution

Run `red-agent schema` to obtain command and operation inventories, request-size limits, exit codes and snapshot scope. Run `red-agent help` for the principal command argument order. The CLI currently has no `--version` contract; record the executable SHA-256 and source revision with a durable run. Invoke it with an argument array, capture stdout and stderr, check the exit code, then parse its JSON stdout. Send UTF-8 request JSON on stdin using `-`, or use an explicit request file.

Exit codes are 0 success, 2 invalid request/usage, 3 input failure, 4 stale input/conflict, and 5 refusal. A structured `code` / `errorCode` refines some failures. These meanings differ from the .NET agent's codes. RED's `schema` is an inventory rather than the .NET focused JSON Schema service.

Requests are limited to 1 MiB while reading and source files to 128 MiB. Captured archives share a 512 MiB byte budget and 100,000-entry budget across one command; index admission occurs before the core parser allocates its entry table. Materializing COG/package archive sets additionally limits aggregate entry payloads to 512 MiB, including overlapping payload ranges. Lighting snapshots share 256 MiB and 4,096 resource lookups across all bakes in a command, including missing lookups. These are agent admission limits, not new format limits on the general core archive API. JSON rejects duplicate object fields, malformed numbers and unescaped control characters; Unicode escape pairs decode as UTF-8 scalars. Invalid integer selectors, flags and overflowing float values refuse before narrowing.

Keep generated levels, build trees, render output and request/result receipts outside source checkouts. Building does not install a new release over an existing distribution.

## Inspect, plan, apply, reopen

1. Inspect the exact input with `inspect` and `statistics`. Query bounded geometry/Thing/light pages and use `select` for spatial targets. Accept a spatial selection only when `match` is `unique`; preserve the returned revision and target.
2. Prepare a patch with `schemaVersion: 1`, the returned `inputSha256` and `sourceRevision`, and an `operations` array. Obtain the exact field contract from the implemented operation family; the generic inventory is not a promise to accept arbitrary legacy fields.
3. Run `plan <input> <patch.json|->`. Keep the returned `outputSha256` with the request. Planning performs no output publication.
4. Run `apply <input> <patch.json|-> <new-output> <outputSha256>`. A new output must use the intended file format; use `convert` for a reviewed format change. If input, resources or operations change, plan again.
5. Reopen the actual output with `inspect`, query the edited fields, and run `validate`. After geometry changes, obtain fresh spatial selections. Check both ends of every adjoin and the intended room path.

The CLI's legacy optional-hash fields remain accepted for compatibility; workflows should supply the hashes and reviewed output argument. Supplying no hash does not establish that a previous plan was approved or remains current.

Agent output uses exclusive sibling staging, complete write/flush/close, then an OS create-only publication. A competing destination is preserved. On POSIX, filesystems without the required hard-link operation fail closed. This is separate from GUI/core asset saves whose explicit contract replaces an existing file.

The command retains a read set for files captured through its bounded input path. After staging and before publication it verifies those files again; changed or unavailable sources cause a conflict and leave no output. Archive consumers parse the admitted byte snapshot rather than reopening a mutable archive for each entry. Lighting bakes separately retain bounded MAT/CMP lookup results, including missing results, and recheck loose-file bytes and winner identity before publication. Captured archive files remain in the source read set. These checks are a pre-publication consistency guard, not a filesystem lock or a claim of full template/resource closure for every importer.

## Geometry and ordinary authoring

`new <output.jkl|output.sed> [mots|jk|droidworks]` creates a closed room scaffold. Placeholder resource names do not provide MAT/CMP bytes or a playable mission. Resolve real resources, templates and scripts before packaging or claiming gameplay.

`.ndy` denotes IJIM text; it is refused for JK, MotS and DroidWorks creation/conversion. `convert` changes the supported container, not the project's game identity.

See [basic design operations](agent_design.md) for box creation, shared-core extrusion/adjoins, material/colormap registration, surface render modes, texel UVs and independent white/RGB corner lighting. Touching geometry is not automatically connected. Basic topology operations deliberately refuse unsupported or ambiguous imported state and geometry they cannot safely classify.

Other exposed families include batch UV projection/transforms, placement arrays, typed COG creation/rebinding, Thing frames, world lighting bake, prefab/DXF operations, and selected legacy geometry recipes. Use current discovery and their core contracts. KEY timeline operations edit standalone animation files; they do not edit Thing mover frames. See [KEY format](formats/key_format.md).

`addThing` creates a fresh instance with the exact requested name. Both it and `placeThingArray` consume optional patch-root `resourceArchives` for the placed-COG append guard; a COG-bearing document without complete source-bound schemas refuses. Appending records also requires a positive next entity ID greater than every existing entity ID. Invalid or reused allocation state remains inspectable for repair, but cannot silently create duplicate identities. No automatic renumbering occurs.

## File and resource preservation

RED `.sed` is a binary native workspace. The older .NET editor also accepted historical text `.sed`; suffix equality does not make those grammars interchangeable. See [native SED policy](sed_format.md) before transforming a workspace. A no-op or native copy retains complete optional sections; an edited workspace with opaque history/buffers refuses until those dependencies can be updated safely. Known RGB/ARCH-only documents remain editable through their supported paths.

JKL/NDY export and legacy JED/NED import have format-specific preservation limits. Keep the source, companion templates and resource provenance. Successful parsing alone does not establish preservation of every historical dialect or unknown record. Follow `convert`'s actual RED signature rather than the .NET resource-request signature.

Resource registration creates a table entry, not the resource itself. Typed COG requests require the exact script/archive evidence expected by that command. Keep archive order, exact entry names and hashes. A package or launch plan does not execute the game.

A package plan's `unverifiedScopes` names every dependency family the planner did not inspect; `runtimeClosureVerified` stays false and `canBuild` only means the explicitly resolved entries can be copied. One permanent scope is `cog-literal-references`: symbol defaults of a completely parsed COG schema are followed, but resources named only as literal call arguments in the code section are not scanned, so a plan containing any COG discloses that scope. Treat a buildable plan as an inventory under those stated limits, not as runtime dependency closure.

The GUI's **File > Save JKL & Test** stages the current document through the normal export boundary without changing its editable project path, dirty state, undo history or ordinary export destination. Save modified COG/resource-editor drafts first. Follow [game testing](game_testing.md) for the selectable Retail/OpenJKDF2 profiles, isolated stage, resource closure and cancellation rules. The agent's `launch-plan` shares the portable planner but does not export or start a process. OpenJKDF2 direct-map startup and retail's **New Game > RED Test** selection are different acceptance paths; retain observations for each game and engine requested.

## Visual and runtime acceptance

`snapshot` and `overview` currently use the built-in `software-v1` renderer for world surfaces. They omit Things/models, sprites and dynamic lighting. Keep camera, resource hashes and render profile fixed for comparisons; inspect the image instead of treating PNG creation as visual approval. Use the editor or declared runtime for excluded content.

Geometry mode zero is hidden even when an adjoin retains a material. Other nonzero modes use filled rasterization; dedicated point/wireframe presentation and exact engine Solid/Textured shading are outside this preview's scope. Orthographic depth uses view-space distance, so nearer surfaces remain in front independently of input order.

Keep structural validation, editor rendering, game loading and player interaction as separate evidence. A three-room portal test does not certify retail loading; a script event does not prove player Use reach. See [capability matrix](capability_matrix.md) and [gap register](gap_register.md) for the remaining product and platform gates. Current-source tests and independent source reviews do not replace native macOS/Linux execution or Windows UI qualification.
