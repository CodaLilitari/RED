# Codex → local generation → Blender

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/local-generation-automation.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Choose the automation route around the useful model result. Comfy is convenient when an artist already has a working graph and wants repeatable inputs; a direct local CLI is often simpler for one region or an isolated alternative generator. Both produce source geometry that still needs artistic selection, game-mesh construction and target-runtime testing.

## Three useful ways to work

| Route | Choose it when | Concrete handoff | Main limitation |
|---|---|---|---|
| Comfy frontend → API export → local bridge | A known graph already works and you want repeated studies or Codex orchestration | API-format graph, exact input files, returned `prompt_id`, history, saved mesh/latent files | Graph/node/checkpoint versions must match the installed server; a UI canvas JSON is a different format. |
| Direct native Comfy-node Python runner | You need explicit latent/decoded-field preservation or a carefully pinned research lane | Exact interpreter, native imports, source hashes, tensor/mesh outputs and execution receipt | Internal node APIs change; no server is required, but the runner is tied to the installed implementation. |
| Model's own CLI or Python pipeline | A generator is easier to isolate than to install into shared Comfy | CLI argv or pipeline arguments, environment/model pins, output GLB and logs | Its preprocessing, cleanup and replay behavior may differ from a Comfy wrapper. |

For a working 16 GB CUDA setup, the retained Hunyuan shape and TRELLIS 512 studies are concrete historical examples. They do not establish that every paint stage, larger resolution or newer model fits. Keep the useful source shape; use local generation for a missing torso, hair mass or other bounded region. Exact teeth, separated digits, hinges and weapon grips often benefit more from manual work or a licensed donor. See [partial generation](partial-generation.md) for the construction choices and measured studies.

## What was checked for this automation guide

On 2026-09-30, a read-only probe of the existing managed installation found ComfyUI **0.30.2** at upstream revision **`dec5d9450a5290bcf63430409ea41018e67f41c3`**, Python **3.13.12**, Torch **2.10.0+cu130**, CUDA **13.0**, and CUDA availability. These interpreter/library values describe that local environment, not a universal requirement. Source inspection found the native classes below. The expected local `127.0.0.1:8188` server was not responsive, and no Comfy Python server was found in the targeted process check. Therefore this guide does **not** claim a successful live `/object_info` response, submission or new generation on that machine.

| Native class observed in that installation | Role |
|---|---|
| `ImageOnlyCheckpointLoader` | Load the matching model, vision encoder and VAE from a checkpoint. |
| `ModelSamplingAuraFlow` | Apply the sampling configuration used by the historical Hunyuan control. |
| `LoadImage`, `CLIPVisionEncode` | Load the prepared image and create image conditioning. |
| `Hunyuan3Dv2Conditioning` | Single-view positive/negative conditioning. |
| `Hunyuan3Dv2ConditioningMultiView` | Front/left/back/right conditioning slots; still needs matching multiview weights. |
| `EmptyLatentHunyuan3Dv2`, `KSampler` | Initialize and sample the Hunyuan latent. |
| `VAEDecodeHunyuan3D`, `VoxelToMesh` | Decode the sampled field and extract a mesh. |
| `SaveGLB` | Save the mesh; its history UI output is under **`3d`**, not `images`. |
| `SaveLatent`, `LoadLatent` | Persist/reload native latent tensors; verify metadata and downstream shape when reusing them. |

The inspected files had no local changes relative to that revision. Use the pinned [Hunyuan implementation](https://github.com/Comfy-Org/ComfyUI/blob/dec5d9450a5290bcf63430409ea41018e67f41c3/comfy_extras/nodes_hunyuan3d.py), [3D saver](https://github.com/Comfy-Org/ComfyUI/blob/dec5d9450a5290bcf63430409ea41018e67f41c3/comfy_extras/nodes_save_3d.py), and [server implementation](https://github.com/Comfy-Org/ComfyUI/blob/dec5d9450a5290bcf63430409ea41018e67f41c3/server.py) for those version-specific contracts; current online tutorials may evolve independently.

The package also retains these three files and the TRELLIS preprocessing/CLI sources for [offline inspection, with original licenses](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/assets/research-sources/README.md). Their exact revisions and hashes are in [source-snapshots.json](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/source-snapshots.json).

No TRELLIS-named source module was found in the inspected native `comfy_extras` files, and the listed custom-node directories did not establish a TRELLIS integration. That is a bounded installation observation, not a claim that Comfy cannot run TRELLIS. The existing isolated **trellis.cpp v0.6.0** executable successfully printed `--help`; this check did not run inference. Earlier full inference remains documented in [generation-studies.json](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/generation-studies.json).

Use the [official Comfy Hunyuan examples](https://docs.comfy.org/tutorials/3d/hunyuan3D-2) to obtain a graph for the intended single-view, multiview or turbo checkpoint. Do not invent a universal node named “Hunyuan”, substitute weights based only on a similar filename, or assume a currently documented node is installed locally.

## Start from a graph that belongs to the installation

1. Open or build the intended local graph in Comfy. Inspect all nodes and their model selections. Prefer the matching official template and the user's already installed weights.
2. Save both the editable frontend workflow and the **API-format** graph using that frontend version's API export command. The latter is a dictionary keyed by node ID, with each node containing `class_type` and `inputs`. The graph links are normally `[source_node_id, output_index]` pairs. Do not send the frontend's `nodes`/`links` canvas object directly to `/prompt`.
3. Identify exact input node IDs, seed/settings node IDs and output saver IDs. Put study output under a new project-specific prefix. Keep filenames in the graph relative to the server's configured input/output directories.
4. Run a read-only capability check before adding a job. The server remains the authority for detailed graph type/link and model-choice validation.

The [upstream API example](https://github.com/Comfy-Org/ComfyUI/blob/master/script_examples/websockets_api_example.py) illustrates an API graph and the request envelope. Its image example is not itself a Hunyuan workflow; use the model-specific graph rather than replacing class names blindly.

Useful routes are summarized below. Query only the classes and job relevant to the task, not the user's complete history or input directory. [Official server route reference](https://docs.comfy.org/development/comfyui-server/comms_routes)

| Request | Use |
|---|---|
| `GET /system_stats` | Runtime/device preflight. Do not publish its raw process arguments or personal paths. |
| `GET /object_info/{class_type}` | Actual class schema, input names, choices, module and output-node status. |
| `POST /upload/image` | Multipart image upload; record the returned `name`, `subfolder` and `type`. |
| `POST /prompt` | Submit `{ "prompt": api_graph, "client_id": unique_id }`; save the returned `prompt_id` immediately. |
| `GET /history/{prompt_id}` | Completion/error record and saver output descriptors for that exact job. |
| `GET /view` | Retrieve a descriptor's file through the API when that route supports the format; omit image-preview conversion parameters when native bytes are required. |
| `WS /ws?clientId=...` | Optional progress and cache events; correlate every event by job ID. |

Native and custom node schemas can expose filename choices. Preserve only the choices relevant to the requested asset in a task report; do not publish incidental input-directory inventories. A loopback service can still contain cloud/API nodes. Use local nodes and review their code/dependencies; the helper below refuses nodes marked as API nodes and modules outside the native node namespaces. This is a narrow compatibility rule, not a sandbox for arbitrary Comfy code.

## Use the included bridge

[comfy_bridge.py](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/scripts/comfy_bridge.py) needs Python 3.10+ and the standard library. It connects to an **already running** Comfy service on this same machine. It does not start Comfy, install dependencies, download weights or alter graphics settings. HTTP is restricted to loopback; environment proxies and redirects are refused. Comfy cloud/partner APIs and custom-node graphs are outside this helper's submission scope.

From the skill directory, query exact native capabilities without adding anything to the queue:

```powershell
python scripts/comfy_bridge.py --url http://127.0.0.1:8188 --node Hunyuan3Dv2Conditioning --node VAEDecodeHunyuan3D --node VoxelToMesh --node SaveGLB
```

Check all classes and required input names in your reviewed API graph:

```powershell
python scripts/comfy_bridge.py --workflow 'D:\model-work\hunyuan-api.json'
```

No `--submit` means no uploads or queue submission. Missing classes, unrecognized module provenance and API nodes are reported. A passing result verifies only node presence and required input names; it does not load checkpoints, verify every enum/type/link or prove inference fits in memory.

When the generation job is within the user's authorized task, explicitly submit it. The example assumes **12** is a `LoadImage` node ID; replace it with the actual ID from your exported graph. `--output-root` must be the server's configured output directory, which may be outside the Comfy code checkout in a managed Desktop installation. Its directory and the parent of `--run-dir` must already exist; the run directory itself must be new.

```powershell
python scripts/comfy_bridge.py --submit --workflow 'D:\model-work\hunyuan-api.json' --input '12.image=D:\model-work\inputs\head-conditioning.png' --output-root 'D:\ComfyData\output' --run-dir 'D:\model-work\runs\head-01' --wait-seconds 900
```

Each `--input` is one exact allowlisted file. Repeat it for multiple native `LoadImage` nodes. The bridge uploads PNG/JPEG/WebP bytes under fresh names with overwrite disabled, uses the **returned** name/subfolder in the graph, and records source hashes. It performs no segmentation, resizing or alpha conversion. The API upload does not mean the graph consumes alpha correctly: the historical Hunyuan path ignored `LoadImage`'s mask output until preprocessing was fixed. Supply the inspected square conditioning image or a graph that explicitly handles alpha and aspect ratio.

For a coding agent, launch a long submission command through its background-process or resumable terminal-session tool. `--wait-seconds 900` is the bridge's job deadline, not a reason for the agent to block silently for fifteen minutes. Retain the process/session ID, read the saved submission response as soon as it appears, and use bounded status/progress checks while reporting meaningful changes. A richer client can consume `/ws`; this small bridge polls history and prints the final result. Do not start a second job merely because the foreground tool call yielded before the first process finished.

The job folder preserves preflight, original/submitted graph, per-upload receipts, submission response, exact job history and an artifact manifest. It copies only supported `type: output` descriptors under the configured output root: native `3d`, `images`, and optional `latents` entries. Supported files include self-contained GLB, OBJ/PLY/STL, common images and native `.latent` tensors. It never deserializes a latent. GLTF with external buffers/material dependencies and custom history shapes require a separate explicit collector; prefer GLB for this handoff. OBJ material sidecars are not automatically gathered, so use it as geometry-only unless dependencies are handled separately.

Artifacts receive generated local filenames, and their original relative server paths, sizes and SHA-256 hashes remain in the manifest. Output traversal, symlink/reparse paths and existing run directories are refused. These are ordinary local-work safeguards, not a claim to defeat a hostile process racing filesystem replacements. Logs/graphs/history are task-local evidence and can contain project text or paths; sanitize before publication.

## Recover the job without duplicating it

After a normal timeout, read the saved `submission.json` or `failure.json` for the accepted `prompt_id`. Leave the server running and collect that exact job into a **new** run directory:

```powershell
python scripts/comfy_bridge.py --resume YOUR-PROMPT-UUID --output-root 'D:\ComfyData\output' --run-dir 'D:\model-work\runs\head-01-recovered' --wait-seconds 300
```

Resume does not upload or submit. A timeout does not cancel a job, clear a queue or imply failure. The helper deliberately never retries `POST /prompt`. If the request reached Comfy but its response was lost, `failure.json` records an unknown submission outcome and `attempt.json` retains the client ID and graph hash. Inspect the exact task's queue/history through the installed Comfy UI or a bounded API query before deciding whether to submit again. Do not treat a missing response as proof that no GPU work happened.

For HTTP validation failure, inspect saved `error-details.json` and its `node_errors`; fix the actual graph mismatch. For execution errors or interruption, preserve the history details and any useful earlier output. The bridge requires explicit successful completion before copying artifacts. Even then, `collected` means files were copied and hashed, not that the mesh passed anatomy or topology review.

The polling client avoids an extra WebSocket dependency. If you want live progress, connect a WebSocket **before** submission, reuse its `client_id`, filter events by `prompt_id`, and finish by reading history. `executed` is a UI-output event, not notification of every completed node. `execution_cached` identifies reused nodes; progress may be absent from nodes without progress hooks. Handle `execution_error` and `execution_interrupted` explicitly; a global queue count is not the completion state of your job. [Official message semantics](https://docs.comfy.org/development/comfyui-server/comms_messages)

## Compare parameters without wasting useful work

Make a small experiment table before the next batch. Hold reference bytes, preprocessing, checkpoint/revision, seed and all other settings constant while changing one variable. Name each case and render its actual mesh with the same neutral cameras.

| Question | Change | Preserve or reuse |
|---|---|---|
| Is the background becoming geometry? | Mask/recenter treatment, one deliberate paired comparison | Native reference, seed/model/settings; inspect both conditioning inputs first. |
| Is a different part reference more useful? | That regional input | Keep the accepted head/other regions; avoid whole-character replacement. |
| Is extraction broken? | Mesh algorithm/threshold on the saved field | Same field and coordinate transform; no new diffusion or VAE decode needed. |
| Would denser decoding help? | Supported VAE decode resolution/chunking | Saved latent where compatible; this is a new decode, not a new sample. |
| Is a colored reference affecting shape? | One controlled material version | Same settings within the pair; disclose any silhouette/shading drift in the edited image. |

Comfy's in-process cache can avoid recalculating unchanged upstream nodes. Do not label every queued graph as a new diffusion sample, and do not assume cache survives a restart, eviction or changed input/model. Record cache events when counting actual generation. Explicit saved latent/field files are the durable recovery mechanism. The included bridge preserves native `SaveLatent` output if present, but does **not** add a decoded-field saver or rewrite your graph.

The installed native `SaveLatent` code writes the `samples` tensor and format marker; the observed `LoadLatent` reconstructs a samples dictionary. Do not assume arbitrary extra conditioning/type metadata survives that round trip. Validate downstream compatibility. For exact field persistence, the historical direct runner saved `sampled-latent.safetensors` and `decoded-voxels.safetensors` before extraction. That runner's field axes, normalization and threshold are documented in [partial-generation.md](partial-generation.md); they are not universal SDF semantics. [Pinned native latent implementation](https://github.com/Comfy-Org/ComfyUI/blob/dec5d9450a5290bcf63430409ea41018e67f41c3/nodes.py)

## Direct CLI alternatives

A direct native-node runner is useful when field/latent preservation matters more than a visual graph. Bind the existing application interpreter, pin the node-source revision, pass exact input/model paths, write to a fresh study directory, and capture stages separately. Preflight imports first. Only the explicit execute branch should load weights/sample/decode. The retained control used `ImageOnlyCheckpointLoader` → vision encode → Hunyuan conditioning → sampler → latent save → VAE decode → field save → extraction. Do not run a copied historical script against an updated installation without checking signatures and output contracts.

For TRELLIS, retain its independent executable and model directory when that route already works:

```powershell
& 'D:\tools\trellis\trellis-cli.exe' --help
& 'D:\tools\trellis\trellis-cli.exe' 'D:\model-work\inputs\part-rgba.png' 'D:\model-work\runs\part-01\candidate.glb' --models 'D:\models\trellis-f16-512' --gpu 0 --res 512 --seed 2026091202 --require-gpu --dump-bg --webp off
```

The second command runs inference; use it only when that trial is intended and its output directory is new. Check the exact version's help rather than assuming these flags apply to another port. In the tested v0.6.0 release, `--no-texture` bypassed the normal cleanup block; complete shape-plus-texture output was a more useful donor. `--dump-slat` in CLI help is not proof that the shipped binary can reload that dump. Preserve logs and the intended final GLB, not only an intermediate PLY. [Pinned TRELLIS CLI reference](https://github.com/pwilkin/trellis.cpp/blob/v0.6.0/README.md)

A new Comfy wrapper for TRELLIS, TripoSG, Hunyuan Paint or another model is an option when it materially improves the workflow. Check its actual node classes, package/revision, weight licenses, memory needs, preprocess and saver implementation in an isolated environment. A hosted API node with a similar model name is a different service and cost path. The bridge intentionally refuses those custom/API paths rather than silently installing them or charging an account.

For an already reviewed **local custom-node graph**, use Comfy's own frontend or a separately reviewed direct API client following the same upload → prompt ID → history → saver-output sequence. Export its real API graph and adapt its actual history output schema; custom mesh nodes may not use native `3d` descriptors. This is a supported workflow choice in the broader guide, even though the included minimal bridge deliberately accepts only native nodes. Pin the extension and its dependencies; keep that route separate from a cloud/partner node or paid service with a similar name.

## Know when to stop generating

If the cranium/socket/cheek volume works but teeth fuse, keep the skull and replace the teeth. If digit count matters, author or fit the correct fingers rather than accepting an attractive wrong hand. If several trials repeat flattened rear hair, fix the reference/conditioning, use consistent multiview input, or choose another donor. More cameras of the same mesh are inspection, not additional generation input.

After one or two useful controlled comparisons, select by region and move to Blender. Check joins, hidden backs, mouth openings, thickness, mirror direction, disconnected fragments and topology. Preserve the best source, then reduce/retopologize, map/bake and bind the intended native animation contract. New GPU samples should answer a concrete unresolved question; they should not postpone precise modeling indefinitely.

## Verification of the included bridge

The bridge's synthetic localhost HTTP tests exercise read-only defaults, exact returned upload naming, `3d` mesh and latent collection, API/custom-node refusal, rejected prompts, failed/interrupted history, timeout/resume semantics, uncertain submission without retries, output containment, remote/redirect rejection and preservation of existing run folders. They use synthetic bytes, not generated models, and do not establish a real Comfy graph run. The actual local capability attempt for this guide returned a connection error; no new generation, downloads or shared-environment edits were performed.
