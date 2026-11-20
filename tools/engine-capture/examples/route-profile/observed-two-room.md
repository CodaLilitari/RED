# Original two-room conversion on OpenJKDF2

The same 33-triangle generated JO BSP was compiled twice by RED with rendered-mesh collision enabled. Source SHA-256 `2b5156487afcfcbf0b6a0af410daac5085ef1d114269c1c7df288b53f6cba609`. Both JK archives used a fixed route, an identical stock input set, the same instrumented engine, and a 640 × 480 drawable. The native driver returned Mesa 26.0.8-1ubuntu0.3, OpenGL 4.5 core, llvmpipe LLVM 21.1.8, 256 bits, swap interval 0, FPS cap 0. Gameplay used ordinary single-player physics. The host allowed concurrent two-worker editor compilation. All 24 launcher and route assertions passed. No graphics hardware or retail engine was timed.

Each range below describes three repeats. The first three columns of measurements are milliseconds per native presentation interval. Each recipe settles before the warmup checkpoint, warms for at least 1.1 s, and measures an approximately 1.5 s window. The walking recipe holds the ordinary forward control for 0.65 s. No framebuffer readback occurs during these windows.

| Route | Geometry | Floor cache | Median | p95 | p99 | Intervals |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| stationary | hybrid-sectors | off | 3.96–4.41 | 4.98–5.69 | 5.40–6.47 | 1064 |
| stationary | hybrid-sectors | on | 3.56–5.27 | 4.68–7.18 | 5.20–8.89 | 998 |
| stationary | model-scene | off | 3.99–5.03 | 4.94–6.29 | 5.20–7.02 | 977 |
| stationary | model-scene | on | 4.02–5.53 | 5.33–10.19 | 5.78–16.62 | 933 |
| walk | hybrid-sectors | off | 4.32–4.71 | 5.69–6.18 | 6.23–6.89 | 989 |
| walk | hybrid-sectors | on | 3.59–5.33 | 4.53–7.06 | 5.19–7.89 | 979 |
| walk | model-scene | off | 4.32–6.05 | 5.68–7.65 | 6.35–8.17 | 892 |
| walk | model-scene | on | 4.46–5.52 | 5.52–7.03 | 5.95–7.82 | 927 |


No eligible comparison improved both median and p95 beyond repeat spread. Both walking geometry comparisons failed the matched-route gate. The same input took the model-scene player to approximately y=0.168 and the hybrid player to y=0.331. Both began at z=0.1201 with identical head angles. Timing cannot approve this change in gameplay behavior. An earlier comparison with rendered-mesh collision disabled placed the stationary model-scene player at z=−0.435, versus z=0.1201 for hybrid. It was excluded before the measured matrix.

The engine recorded actual sector visibility and frustums, submitted 3DO faces and model draw calls with a submitted face, Thing intersection queries, exact cache activations and cache hits. A submitted face can still be occluded. Transparent openings, silhouette equivalence, all brush collision semantics, per-stage GPU/CPU time, and hardware speed remain unqualified. The external engine observer and recipes in this folder reproduce the measurement interface without stock game data.

| Exact input | SHA-256 |
| --- | --- |
| engine | `e28b46b3375ccc41352796cbefa5af648ea3e61e805dc9afdee01d2b4b56db76` |
| hybrid-sectors | `16bee47528f6a24485d6aa4a3a9ce7f0a733b7d60560b5c55ab6851414f3b5c2` |
| stationary recipe | `df69d549d09ed55318f1c1fe40a577cc2406e8e5faac135d2f07e118aebcee4e` |
| model-scene | `64962e6c7afeb9a805a341407d7fe87947d045451f721c05f4342faf17af9464` |
| walk recipe | `4d682832ba032c9f5bca03ef15324f0aa364e7cba8a4a7bbf076ab02e1bb52ca` |

CPU reported by the host: 12th Gen Intel(R) Core(TM) i9-12900K.
