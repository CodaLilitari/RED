# Partial generation and donor assembly

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/partial-generation.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

For concrete Codex-to-runner/API execution, read [local-generation-automation.md](local-generation-automation.md). It covers verified node names, workflow export, exact input mapping, job IDs, recovery and retained mesh/latent output; this reference explains which parts to generate and how to judge them.

Use image-to-3D for bounded source geometry: a head volume, shoulder mass, hand, boot, hair clump, garment or prop. Preserve a useful part and repair the specific failure. A convincing source render is not evidence of low-poly topology, KEY compatibility, successful 3DO export, or single-player/multiplayer behavior.

This guide distills executed local studies. It distributes methods and provenance requirements, not their private reference images, character meshes, checkpoints or anatomical donors. Historical runtime numbers are observations on one machine, not performance promises or minimum requirements.

## Pick a route that matches the job

You do not need image-to-3D for every model. Offer these options and recommend the smallest route that addresses the user's actual bottleneck.

| Route | Choose it when | Main tradeoff | Useful recovery when it fails |
|---|---|---|---|
| Manual Blender modeling or an authorized stock-model edit | The shape is simple, the rig must remain predictable, or exact proportions matter most | More direct modeling time; no inference setup | Preserve the stock animation contract; use image references or a small donor for the difficult region. |
| Licensed donor kitbash | A suitable anatomical, clothing or mechanical part already exists | Fit, topology, style and license reconciliation | Retain only the useful region and author the join; do not replace a successful whole head just to fix its teeth. |
| Hunyuan coarse part generation | Organic volume is the bottleneck and a suitable local runtime fits the machine | Fast rough volume, unreliable thin gaps, dentition and unseen detail | Fix conditioning; extract the saved field; split the problem; use a precise donor. |
| TRELLIS texture-bearing donor | Layered cloth/hair/back structure or a textured source is useful | More dependencies and a version-specific cleanup path; topology still needs inspection | Inspect the complete pipeline's GLB; keep selected regions and discard bad extremities. |
| Generated concept, then manual or donor-based geometry | The visual design is unresolved or a novel shape needs reference development | A good image is not a consistent 3D design | Make a single regional reference, check profile/back logic, then choose one of the geometry routes above. |
| Consistent multiview generation | The hidden side matters and consistent views can be supplied | More input preparation and matching model requirements | Resolve view contradictions before another seed; a licensed full mesh may be cheaper. |

On a modest GPU or CPU-only system, manual/donor work remains a complete option. On a working 16 GB CUDA system, the historical Hunyuan shape lane and isolated TRELLIS 512 lane provide concrete starting examples. Offer a local preflight instead of promising that another model family, higher resolution or paint stage will fit.

## Choose what to generate

Before sampling, write a short regional contract: silhouette, view, scale landmarks, required openings, digit count, material boundaries and attachment area. For example: “one hand and forearm; two long fingers plus one thumb; every gap visibly open; retain enough wrist for joining.” The contract should describe the actual design, not assume human anatomy.

| Need | Preferred starting point | Reason and acceptance check |
|---|---|---|
| Coarse organic volume | One bounded image-to-3D trial | Inspect actual mesh from front, side, back and three-quarter views. |
| Exact mechanical sockets, teeth, hinges or digit count | Authored geometry or a licensed donor | Verify count, separation and fit; a generated image cannot certify geometry. |
| A good head with fused teeth | Keep the head; graft/author dentition | Preserve brow, socket, cheek, nose and jaw silhouette that already work. |
| Consistent rear structure | Licensed source mesh or real consistent multiview conditioning | More cameras rendered after single-view generation do not supply missing input evidence. |
| Rig and animation | Target stock hierarchy/KEY contract | Treat generated geometry as an unrigged donor. |

Whole-character regeneration often sacrifices accepted details to improve one region. Keep alternatives separate, record why each region wins, and assemble a deliberate hybrid. In the source studies, a generated skull contributed useful volume but fused teeth; a later dental graft retained 28 separate anatomical teeth. Different body samples supplied better torso, arm, leg, cloth and mane regions. No single generator supplied a finished character.

## Pin the actual execution lane

Record an environment receipt before a new run:

- Exact interpreter or executable path, Python, Torch, CUDA, GPU, available VRAM/RAM, OS, and runtime version or commit.
- Checkpoint repository, immutable revision, relative filename, byte size, SHA-256 and license source.
- Workflow JSON or runner script hash, node/API versions and relevant installed-source hashes.
- Native reference hash, alpha mask hash, conditioning image hash, crop/recenter operation and output resolution.
- Seed, sampler, scheduler, steps, guidance, latent token count, VAE/decode resolution, extraction threshold and algorithm.
- Separate output directory, expected outputs, time limit and policy for retrying a failed run.

Use the interpreter belonging to the application actually running. A Comfy Desktop bootstrap or standalone interpreter can lack Torch even when the managed Comfy environment works. A successful import, CPU model load, downloaded checkpoint or workflow file is only preflight. It does not establish GPU inference or a usable mesh.

Prefer existing native Comfy nodes when they satisfy the task. The historical lane called native nodes directly from Python, without starting a Comfy server or installing custom nodes. This was a version-pinned integration, not a universal stable API. For a new environment, start from the [official Comfy Hunyuan examples](https://docs.comfy.org/tutorials/3d/hunyuan3D-2), check the node signatures and save the actual runnable workflow. Keep an experimental runtime isolated from a working shared installation.

### Historical Hunyuan control

The following combination completed a head study and subsequent bounded body/part studies. It is a reference configuration, not an instruction to downgrade a working installation.

| Item | Recorded value |
|---|---|
| Checkpoint | `tencent/Hunyuan3D-2`, `hunyuan3d-dit-v2-0/model.fp16.safetensors` |
| Model revision | `9cd649ba6913f7a852e3286bad86bfa9a2d83dcf` |
| Weight bytes / SHA-256 | `4928151562` / `360bc281fc956d4acac0c3d36d5ec0ebf8cdddbf4b8892e894d12419388d479b` |
| Comfy source revision recorded by runner | `dec5d9450a5290bcf63430409ea41018e67f41c3` |
| Runtime | Python 3.13.12, Torch 2.10.0+cu130, CUDA 13.0, RTX 5070 Ti |
| Sampler | Euler, normal scheduler, 20 steps, CFG 8, denoise 1, AuraFlow shift 1 |
| Latent / decode | 3072 latent tokens, batch 1, chunks 8000; head decode 256, later body/part decode 384 |
| Vision input | DINO 518 × 518 in this installed lane |
| Head timing / peak allocation | 43.730 s for inference/export after preflight; 5,492,669,952 allocated CUDA bytes (about 5.12 GiB) |

The native node sequence was checkpoint load → AuraFlow sampling patch → image/vision encode → Hunyuan conditioning → empty Hunyuan latent → sampler → save latent → VAE decode → save field → mesh extraction → save GLB and mesh tensors. Keep the latent and field before attempting downstream repairs. Shape generation with a colored input still produces an untextured mesh in this lane. Paint is a separate pipeline.

Full, fast, turbo, single-view and multiview checkpoints have different intended settings. Do not apply the historical CFG/step values to every variant. The [official multiview example](https://github.com/Tencent-Hunyuan/Hunyuan3D-2/blob/main/examples/shape_gen_multiview.py) loads the matching `Hunyuan3D-2mv` weights and separately labelled views. A front/back collage sent to the single-view encoder is not equivalent.

## Preprocess before spending a seed

A plain white RGB background is not an alpha mask. Inspect the actual conditioning tensor/image, not just the native source.

1. Preserve the accepted native reference untouched. Generate or derive a foreground mask as a separate artifact.
2. Inspect the mask over a contrasting checkerboard at original resolution. Check pale hair tips, cloth fringes, gaps between digits, holes between limbs and torso, and removal of ground shadows.
3. Find the accepted foreground bounds. Fit the foreground proportionally into a square with a recorded border; the historical Hunyuan correction used 15% total border and a white composite.
4. Resize that prepared square to the encoder's actual expected size. Preserve the full-resolution square and final encoder image for review.
5. Record source RGB preservation, mask model/version, thresholds/matting settings, background color and every crop/scale operation.

The [Tencent preprocessor](https://github.com/Tencent-Hunyuan/Hunyuan3D-2/blob/main/hy3dgen/shapegen/preprocessors.py) uses alpha bounds and aspect-preserving recentering. Its usual image output size and your installed vision encoder size may differ; the local study explicitly made a 518-square derivative for its DINO encoder.

**Slab failure:** the original local body and torso runner ignored the loaded mask and sent portrait RGB directly through `crop=False`. The installed implementation resized to a square, stretching the portrait. Both outputs reconstructed large backing slabs. A combined correction—foreground removal, aspect-preserving recentering, white composite—eliminated the slabs in same-seed reruns. This supports the combined repair; it does not isolate the individual causal contribution of masking versus aspect ratio. Preserve the failed samples and the paired settings.

Do not automatically upscale before shape generation. The local DRCT and RealESRNet probes established CPU loading only. Increasing source resolution above a fixed 518-square conditioning input does not add hidden rear geometry. Upscaling can also alter edge/texture cues, so it is another experimental variable.

## One saved field, several inspections or extractions

Treat these as different operations in your run ledger:

| Operation | New diffusion sample? | New VAE decode? |
|---|---:|---:|
| Render another camera of a saved mesh | No | No |
| Re-extract a mesh from the saved decoded field | No | No |
| Decode a saved latent at another supported resolution | No | Yes |
| Change seed/input and run the generator | Yes | Yes |

In one executed study, native surface-net connectivity failed. A Lewiner marching-cubes extraction of the same field at level 0.6 completed in 1.275 s and yielded 316,624 triangles with one connected component and no boundary or over-subscribed edges in the recorded topology check. No second generative sample was required. The useful result still had fused teeth, a nearly closed jaw and an unwanted temporal boss.

Extraction must preserve the saved field's meaning:

- Record array keys, shape, dtype, range, field hash and axes. The observed Comfy field was clipped `(decoder_logits + 1) / 2`, not raw signed distance. A threshold of zero would have been wrong for that field.
- The observed stored order was `z,y,x`. Reversing axes changes handedness; verify outward winding using inside/outside field samples.
- Read the actual resolution from the receipt. Do not hard-code a 257³ field when a later study used 385³.
- Match the original mesh's coordinate normalization when a rig or assembly is already aligned. Decoder physical coordinates and the native extractor's display transform were not identical.
- Check whether the isosurface reaches the volume boundary. Reject or explicitly label an open-boundary diagnostic; do not quietly fill it.
- Preserve original field, latent and mesh hashes. Write the derivative to a new path with algorithm and transformation details.

Report finite coordinates, triangle count, components, edge incidence, inconsistent winding, duplicate faces, zero-area faces and bounds. State the measurement method. UV and normal seams can duplicate positions without being physical holes; distinguish raw index topology from topology after exact-position seam identification. None of these checks proves absence of geometric self-intersections or correct anatomy.

## Alternative generator: version-specific TRELLIS lesson

An isolated Windows `trellis.cpp` v0.6.0 trial completed at 512 resolution with F16 weights. The source commit was `06fc9000719c912ddc4929d21db075972c26ac3e`; the release archive SHA-256 was `4d08ab27e83094035fd8349aaf34d3460738df0466ef9c4991ddd958c0344bc2`. These identify a historical tested release, not a current recommendation to use an old binary. Refer to its [pinned README](https://github.com/pwilkin/trellis.cpp/blob/v0.6.0/README.md) and audit a different release separately.

The important finding was pipeline behavior: in that release, `--no-texture` skipped the normal weld/remesh/cleanup/QEM/UV block. Its raw geometry was perforated and heavily nonmanifold. The complete shape-plus-texture path with the same source and seed produced byte-identical pre-cleanup geometry, then a much cleaner 146,810-triangle GLB. It still had 2,172 edges incident to more than two faces after exact-position seam identification. It was useful for selected cloth/back/hair donors, not accepted as a complete character. The [pinned CLI source](https://github.com/pwilkin/trellis.cpp/blob/v0.6.0/src/trellis_cli.cpp) explains the conditional postprocessing.

Portable command shape for that pinned release, after separately installing its runtime and licensed weights:

```powershell
# Set these to exact local files/directories you have verified.
$trellisCli = 'D:\tools\trellis-v0.6.0\trellis-cli.exe'
$inputRgba = 'D:\model-work\inputs\accepted-foreground.png'
$modelDir = 'D:\models\trellis-f16-512'
$outputGlb = 'D:\model-work\studies\candidate-01\candidate.glb'
& $trellisCli $inputRgba $outputGlb --models $modelDir --gpu 0 --res 512 --seed 2026091202 --require-gpu --dump-bg --webp off
if ($LASTEXITCODE -ne 0) { throw 'TRELLIS inference failed; preserve the log.' }
```

Create a new output directory first; refuse to overwrite an existing study. Capture the actual argv, environment, stdout/stderr, exit code and output hashes. Use its final GLB for the cleaned result; an intermediate PLY can represent a different stage. A replay helper present in source is not necessarily shipped in a release. This release's portable CLI had no supported latent-replay option, so the complete-path comparison legitimately reran the short geometry stage. Do not claim “same saved field” when it was a same-seed rerun.

Preprocessing also differed by architecture: the historical TRELLIS path accepted the source RGBA, cropped around its alpha, composited against black and resized to 512. Hunyuan used a white 518-square derivative. That was a same-source comparison, not an identical-conditioning-tensor comparison.

## Fit donors while preserving evidence

Keep raw donors read-only and operate on copies. Record an object/region map, source hashes and transforms for each retained part. When an anatomical dataset shares coordinates, import the set together; independently centering every tooth destroys the dental arrangement. Preserve concept/component labels rather than inferring upper/lower anatomy from filenames.

Align stable landmarks first. Check neutral front/side/back cameras before cutting donor surfaces. Remove redundant hidden anatomy and doubled limbs deliberately. Recheck silhouette and joins after each bounded operation: shoulders, wrists, knees, neck, scalp and cloth hems often fail in different views. A selected face count, successful boolean or GLB reimport does not replace those views.

Freeze the accepted source assembly before retopology, reduction, UVs and final baking. The historical high-resolution assemblies were approximately one million triangles and explicitly remained source studies. Do not ship that stage as a retail or multiplayer model.

## Donor and model licensing

Maintain a per-component ledger: author/publisher, asset or weight name, source URL, revision, acquisition date, hash, license document, required credit, original notices and modifications. Code, neural weights, encoders, input art and output character rights are separate entries.

For example, the [BodyParts3D archive license](https://dbarchive.biosciencedbc.jp/en/bodyparts3d/lic.html), updated 2025-02-27 and checked for this guide on 2026-09-30, offers CC BY 4.0 and requests: “BodyParts3D, © The Database Center for Life Science licensed under CC Attribution 4.0 International”. The historical 2013 OBJ headers retained older terms; preserve both the raw notices and the current publisher offer instead of silently deleting evidence. Do not assume another anatomy project's modifications have the same terms.

Hunyuan uses its own [community license](https://huggingface.co/tencent/Hunyuan3D-2/blob/9cd649ba6913f7a852e3286bad86bfa9a2d83dcf/LICENSE), including territory restrictions. Review the applicable version before use or redistribution. Likewise, an MIT runtime does not make its entire model bundle MIT: TRELLIS's DINOv3 dependency has [separate Meta terms](https://github.com/facebookresearch/dinov3/blob/6876159a11b4df116f30f667f8c9888617df0751/LICENSE.md). Keep weights and third-party meshes outside this workflow repository.

## Handoff gate

Deliver a candidate receipt containing: accepted reference and conditioning hashes; runtime/checkpoint pins; run/extraction counts; raw and selected derivative hashes; topology report; matched neutral views; regional acceptance/rejection notes; donor/license ledger; and the exact remaining work. Label the stage explicitly: `source-study`, `cleaned-source`, `game-mesh`, `exported`, or `runtime-tested`. Record single-player and multiplayer results separately downstream.

## Hosted Tripo inputs

A supplied Tripo GLB is a separate starting route from local TripoSG or selective Hunyuan/TRELLIS inference. Preserve the original and inspect what actually generated. Red’s two Mandalorian examples show useful low-poly source material followed by substantial rigid segmentation, fitting and texture work. Follow [Tripo to JK](tripo-to-jk.md), and keep any further generation within the user’s requested scope and current service allowance.
