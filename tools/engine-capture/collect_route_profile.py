"""Run two packaged engine requests through the installed private gameplay runner on Linux."""
import argparse
import copy
import hashlib
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys

from route_performance import compare, decode, require


def read(path, limit):
    with path.open("rb") as stream:
        data = stream.read(limit+1)
    require(len(data) <= limit, f"Input exceeds size budget: {path}")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("model_request","hybrid_request","stationary_recipe","walk_recipe","output"):
        parser.add_argument(name,type=Path)
    parser.add_argument("--cpu",required=True,help="recorded CPU model")
    parser.add_argument("--class",dest="hardware", choices=("hardware","software"),required=True)
    parser.add_argument("--physics",required=True,help="recorded engine physics settings")
    args=parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("The ordinary gameplay runner requires Linux.")
    requests={key:decode(read(file,1<<20)) for key,file in (("model-scene",args.model_request),("hybrid-sectors",args.hybrid_request))}
    recipes={key:decode(read(file,1<<20)) for key,file in (("stationary",args.stationary_recipe),("walk",args.walk_recipe))}
    output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=False)
    runner=Path(__file__).resolve().with_name("invoke_engine_capture.py")
    def ref(path):
        return {"path":path.relative_to(output).as_posix(),"sha256":hashlib.sha256(read(path,8<<20)).hexdigest()}
    report={"schemaVersion":1,"title":"Paired route profile","runs":[]}
    conditions=list(itertools.product(recipes,requests,("off","on")))
    for repeat,order in enumerate((conditions,list(reversed(conditions)),conditions[3:]+conditions[:3]),1):
        for route,geometry,cache in order:
            trial=output/f"{repeat}-{route}-{geometry}-{cache}"
            trial.mkdir()
            request=copy.deepcopy(requests[geometry]);request["gameplay"]=recipes[route]
            spec=trial/"request.json";spec.write_text(json.dumps(request),encoding="utf-8")
            session=trial/"session"
            deadline=request.get("runnerTimeoutMs",180000)/1000+30
            require(0 < deadline <= 1830,"Invalid runner deadline")
            proc=subprocess.run([sys.executable,"-B",str(runner),str(spec),"--output",str(session)],
                                cwd=trial,env={**os.environ,"OPENJKDF2_FLOOR_MISS_CACHE":str(int(cache=="on"))},timeout=deadline)
            sys.stderr.write(f"{trial.name} exited {proc.returncode}\n")
            launcher=session/"launcher-receipt.json";outcome=decode(read(launcher,4<<20))
            agent=session/"agent";native=agent/"route-target.txt";mode=agent/"route-cache.txt"
            if not native.is_file() or not mode.is_file() or not (session/"scenario.json").is_file():
                raise ValueError(f"Trial {trial.name} lacks native observations. Inspect {launcher} for its outcome.")
            target=dict(line.split("=",1) for line in read(native,4096).decode().splitlines())
            engines=[r["sha256"] for r in request["files"] if r["role"]=="engine"]
            require(len(engines)==1,"Expected one engine")
            report["runs"].append({"route":route,"geometry":geometry,"cache":cache,"repeat":repeat,
                "target":{"renderer":target["renderer"],"driver":target["driver"],"hardwareClass":args.hardware,
                    "cpu":args.cpu,"resolution":[int(target["width"]),int(target["height"])],"vsync":int(target["swapInterval"]),
                    "fpsCap":int(target["fpsCap"]),"physics":args.physics,"clock":"native pre-swap monotonic nanoseconds"},
                "packageSha256":outcome["episodeArchive"]["archiveSha256"],"engineSha256":engines[0],
                "recipeSha256":ref(session/"scenario-input.json")["sha256"],
                "frames":ref(agent/"route-frames.csv"),"launcher":ref(launcher),"scenario":ref(session/"scenario.json"),
                "staged":ref(session/"manifest-before.json"),"nativeTarget":ref(native),"observedCache":ref(mode)})
            manifest=output/"profile.json";tmp=output/"profile.tmp"
            tmp.write_text(json.dumps(report,indent=2),encoding="utf-8");tmp.replace(manifest)
    compare(report,output)
    print(manifest)


if __name__=="__main__":
    try:
        main()
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:
        sys.exit(str(error))
