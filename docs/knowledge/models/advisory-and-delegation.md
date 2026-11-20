# ChatGPT Pro advisory and practical delegation

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/advisory-and-delegation.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Use an assistant for a concrete uncertainty: which source part is worth preserving, whether a hand pose reads correctly, how to divide a large rig, whether a proposed bake will lose silhouette, or which historical claim needs a source. A second opinion is most useful before an expensive irreversible branch or when local iterations have stopped improving the result.

## Available roles

| Role | Useful work | Best input |
| --- | --- | --- |
| ChatGPT Pro visual adviser | Anatomy, silhouette, alternative construction, matched comparisons, critique of game-sized appearance | Actual reference pixels and labeled renders of the current mesh |
| ChatGPT Pro research adviser | TDIR/JKE distinctions, primary-source leads, counterexamples and plan criticism | A bounded technical packet, source links and precise unknowns |
| Codex implementation worker | One Blender helper, parser, exporter test or isolated component | Exact working folder, owned files, expected artifact and permitted actions |
| Independent checker | Export roundtrip, counts, manifests, source claims and package behavior | Frozen candidate plus acceptance criteria, preferably without the author's conclusion |
| Human artist/player | Visual acceptance, control feel, animation readability and multiplayer experience | Playable candidate and short targeted test scenarios |

Choose only the roles that help. A simple material recolor usually does not need a panel of reviewers.

## Practical ChatGPT route

Use an existing project-specific saved conversation or create one when authorized. Verify the current account, actual model selection and Pro mode in that product. Model names and availability change; this public skill does not hard-code a paid model or account. A Codex worker is a separate route from a ChatGPT Pro conversation.

Use the user's existing subscription if available. Do not substitute an API request, paid overage or newly purchased service merely because it is easier to automate. If the available route cannot accept an original file, explain the transformation to a rendered image or text excerpt; a description of an image is not the image itself.

Upload only the selected project files. Prefer native files when the product actually supports their useful contents. For model review, matched PNG renders often give a more reliable visual packet than assuming an assistant can inspect an arbitrary `.blend` or `.3do`. Include front/side/back and relevant joints, a neutral material view, the textured view and a gameplay-scale crop when these answer the question. Label each as source render, generated concept, roundtrip export or game capture.

Official documentation distinguishes ChatGPT projects from local projects: a ChatGPT project does not automatically expose a local folder. Supply selected files through supported uploads/connections. [OpenAI: Projects and chats](https://learn.chatgpt.com/docs/projects), [ChatGPT on the web](https://learn.chatgpt.com/docs/web).

## Ask for a decision, not general encouragement

Good question: “Here are three matched views of candidates A and B. The target is a 200-pixel-tall retail character. Which skull/neck transition should we keep, and which two local changes would improve it most? Identify uncertainty and do not infer hidden geometry.”

Good research question: “Does this particular TDIR model rely on custom KEY/PUP data? Separate archive measurements, author statements and renderer inference; cite the source for each.”

Good delegation: “Own only `tools/bake_color.py` in this worktree. Produce a headless color-only bake of the supplied source onto the supplied destination, without modifying either input. Return the output texture, command, versions and a matched preview.”

Ask for alternatives with tradeoffs, concrete corrections and counterexamples. Avoid leading independent reviewers with the result you want them to approve. Give a reviewer your proposed result when the task is specifically to review that result.

## Continue and verify

Record the conversation reference, selected visible model/mode, exact packet or its hash, returned response, completion state and useful findings in the local project. Wait for an actual completed answer; a submitted prompt, tool-limit notice or partial reply is not a finished review. Inspect an ambiguous send before retrying. Continue in the same saved conversation and acknowledge useful contributions.

Check recommendations against the source and appropriate local tests. A model's claim to have executed a game or inspected an unavailable file is not a local test result. Distinguish a correct source citation from a correct technical conclusion. Preserve a short decision log: adopted, rejected or unresolved, with the reason. Publish the useful technical synthesis and credits; keep unrelated private context and access details out of the distribution.

For reusable multi-worker work, assign independent ownership: one worker can research compatibility, another prepare texture options and another validate export behavior. One coordinator integrates the choices and checks shared assumptions. Do not let simultaneous workers overwrite the same `.blend` or shared output name.
