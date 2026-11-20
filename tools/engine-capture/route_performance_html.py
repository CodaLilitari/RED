"""Portable route profile report. All labels and input-derived values are escaped."""
from html import escape


def fmt(value):
    return "Unavailable" if value is None else f"{value:.3f}"


def table(headings, rows):
    head = "".join(f'<th scope="col">{escape(str(h))}</th>' for h in headings)
    body = "".join("<tr>" + "".join(f"<td>{escape(str(value))}</td>" for value in row) + "</tr>" for row in rows)
    return f'<div class="scroll"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>'


def route_plot(points, field, title):
    width, height = 540, 150
    values = [point[field] for point in points]
    if any(value is None for value in values):
        return f"<p>{escape(title)} unavailable.</p>"
    lo, hi = min(values), max(values)
    def xy(i, v):
        return f"{15+(width-30)*i/max(len(values)-1,1):.1f},{height-24-(height-42)*(v-lo)/max(hi-lo,1e-9):.1f}"
    line = " ".join(xy(i, v) for i, v in enumerate(values))
    caption = f"{title} • {lo:g} to {hi:g}"
    return (f'<figure><figcaption>{escape(caption)}</figcaption><svg role="img" aria-label="{escape(caption)}" '
            f'viewBox="0 0 {width} {height}"><path d="M15 12V{height-24}H{width-15}" fill="none" stroke="#abbac0"/>'
            f'<polyline points="{line}" fill="none" stroke="#006c68" stroke-width="2.5"/></svg></figure>')


def map_plot(points):
    axes = [[p[k] for p in points] for k in ("x","y")]
    spans = [max(v)-min(v) for v in axes]
    scale = 116/max(*spans,.08)
    centers = [(max(v)+min(v))/2 for v in axes]
    coordinates = [(180+(p["x"]-centers[0])*scale,78-(p["y"]-centers[1])*scale) for p in points]
    palette = ("#0f6b63","#b45211","#714abb","#1a5eab")
    segments = []
    for idx, (a,b) in enumerate(zip(coordinates, coordinates[1:])):
        color = palette[points[idx]["sector"]%len(palette)]
        segments.append(f'<line x1="{a[0]:.2f}" y1="{a[1]:.2f}" x2="{b[0]:.2f}" y2="{b[1]:.2f}" stroke="{color}" stroke-width="3"/>')
    start, end = coordinates[0], coordinates[-1]
    text = "Player path in world X/Y. Circle marks start; square marks end. Line color indicates sector."
    shapes = f'<circle cx="{start[0]:.2f}" cy="{start[1]:.2f}" r="4" fill="#111"/><rect x="{end[0]-3:.2f}" y="{end[1]-3:.2f}" width="6" height="6" fill="#c53b26"/>'
    return f'<figure><figcaption>Player path · world X/Y</figcaption><svg role="img" aria-label="{text}" viewBox="0 0 360 156">{"".join(segments)}{shapes}</svg></figure>'


def render(report):
    title = escape(report["title"])
    parts = [f"""<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>{title}</title><style>
*{{box-sizing:border-box}}body{{font:16px/1.5 system-ui,sans-serif;color:#182c32;background:#fafbf9;margin:auto;padding:2rem;max-width:78rem}}
h1,h2,h3{{line-height:1.15}}h1{{font-size:2.3rem}} h2{{margin-top:2rem}}table{{width:100%;border-collapse:collapse}}
th,td{{padding:.45rem .55rem;border-bottom:1px solid #c4cfce;text-align:left}}th{{background:#e8efed}}
.scroll{{overflow-x:auto}}details{{margin:.9rem 0}}summary{{font-weight:600;cursor:pointer}}.plots{{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:.7rem}}figure{{margin:0;background:white;padding:.6rem}}
figcaption{{font-size:.85rem;font-weight:600}}svg{{width:100%;height:auto}}small{{overflow-wrap:anywhere}}@media print{{body{{padding:0}}}}
</style><main><h1>{title}</h1>"""]
    target = report["target"]
    parts.append(table(("Target condition","Observed or declared value"),target.items()))
    parts.append("<h2>Paired comparisons</h2>")
    rows = []
    for comparison in report["comparisons"]:
        metrics = comparison.get("metrics")
        verdict = comparison.get("reason") or ("Median and p95 fall below repeat spread." if comparison["exceedsRepeatSpread"]
                                               else "A gain beyond repeat spread was not observed.")
        rows.append([comparison["route"],comparison["baseline"],comparison["candidate"],
                     *[fmt(metrics[k]["pairedMedianDelta"] if metrics else None) for k in ("medianMs","p95Ms","p99Ms")],
                     verdict, comparison["optimizationStatus"]])
    parts.append(table(("Route","Baseline","Candidate","Median Δ ms","p95 Δ ms","p99 Δ ms","Timing result","Optimization decision"),rows))
    parts.append("<h2>Repeated routes</h2>")
    for group in report["conditions"]:
        label = " / ".join(group[k] for k in ("route","geometry","cache"))
        parts.append(f'<h3>{escape(label)}</h3>')
        rows = []
        for r in group["runs"]:
            rows.append((r["repeat"],r.get("samples",0),*(fmt(r.get(k)) for k in ("warmupMs","medianMs","p95Ms","p99Ms","distance")),
                         r.get("reason","Passed declared route")))
        parts.append(table(("Repeat","Intervals","Warmup ms","Median ms","p95 ms","p99 ms","Travel (JKU)","Observation"),rows))
        for r in group["runs"]:
            if not r["accepted"]:
                continue
            parts.append(f'<details><summary>Repeat {r["repeat"]} observations and identities</summary><div class="plots">')
            points = r["overlay"]
            parts.extend((map_plot(points),route_plot(points,"sectorFrustums","Portal clip frustums per frame"),
                          route_plot(points,"thingCollisionQueries","Thing collision queries per frame"),
                          route_plot(points,"attachmentFlags","Native player attachment flags")))
            parts.append("</div>")
            parts.append(table(("Native counter","Maximum within window"),((k,fmt(v)) for k,v in r["counterMax"].items())))
            for key in ("engineSha256","packageSha256","recipeSha256","traceSha256","launcherSha256"):
                parts.append(f'<p><small>{key} · {escape(r[key])}</small></p>')
            parts.append("</details>")
    if report["semanticChanges"]:
        parts.append("<h2>Declared changes and review limits</h2><ul>"+"".join(f"<li>{escape(v)}</li>" for v in report["semanticChanges"])+"</ul>")
    parts.append(f'<p>{escape(report["limits"])}</p></main></html>')
    return "\n".join(parts)
