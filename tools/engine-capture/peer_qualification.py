"""Verify a named multiplayer scenario against two independently observed game sessions."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

from route_performance import Evidence, decode, identity, percentile, require


def actor(row, player_id):
    return next((player for player in row["players"] if player["id"] == player_id), None)


def movement(local, remote, player_id):
    def endpoints(rows):
        positions = [p["position"] for row in rows if (p := actor(row, player_id))]
        return (positions[0], positions[-1]) if len(positions)>=2 else None
    a, b = endpoints(local), endpoints(remote)
    if not a or not b:
        return {"passed":False,"movedLocally":False,"reason":"Missing one endpoint during the action"}
    distances = [math.dist(*positions) for positions in (a,b)]
    errors = [math.dist(a[i],b[i]) for i in (0,1)]
    return {"passed":min(distances)>=.05 and max(errors)<=.05,"movedLocally":distances[0]>=.05,
            "localTravel":distances[0],"remoteTravel":distances[1],"startError":errors[0],"endError":errors[1]}


def lines(content, limit=10000):
    data=content.splitlines()
    require(0 < len(data) <= limit, "Empty or oversized observation stream")
    return [decode(row) for row in data]


def read_endpoint(refs, role, evidence):
    deployment = decode(evidence.read(refs["deployment"],8192))
    require(deployment["role"]==role and type(deployment["processId"]) is int and deployment["processId"]>0,
            "Invalid endpoint deployment")
    for key in ("packageSha256","engineSha256","configSha256"):
        identity(deployment[key])
    require(deployment["packageSha256"]==evidence.read(refs["packageAfter"],128).decode().strip(),
            "Packaged episode changed after launch")
    rows=lines(evidence.read(refs["observations"],24<<20))
    previous=-1
    own_id=None
    for row in rows:
        require(type(row["ms"]) is int and row["ms"]>previous and row["server"] is (role=="host")
                and row["world"]==deployment["world"], "Time, multiplayer role, or loaded world differs")
        previous=row["ms"]
        require(0 <= row["occupied"] <= row["capacity"] and isinstance(row["players"],list)
                and len(row["players"])<=32, "Invalid runtime occupancy")
        ids=set();selves=[]
        for p in row["players"]:
            require(type(p["id"]) is int and p["id"]>0 and p["id"] not in ids and p["sector"]>=0
                    and len(p["position"])==3 and all(math.isfinite(x) for x in p["position"])
                    and math.isfinite(p["health"]), "Invalid native player")
            ids.add(p["id"])
            if p["self"]:selves.append(p["id"])
        if selves:
            require(len(selves)==1 and own_id in (None,selves[0]), "Endpoint local player identity changed")
            own_id=selves[0]
    require(own_id is not None,"No local multiplayer actor was observed")
    raw_contacts=evidence.read(refs["contacts"],4<<20)
    contacts=lines(raw_contacts) if raw_contacts.strip() else []
    timing=[int(v) for v in evidence.read(refs["frames"],1<<20).splitlines()]
    require(21<=len(timing)<=60000 and all(b>a for a,b in zip(timing,timing[1:])), "Invalid native presentation clock")
    dt=[(b-a)/1e6 for a,b in zip(timing,timing[1:])]
    device=dict(v.split("=",1) for v in evidence.read(refs["target"],4096).decode().splitlines())
    require(all(device.get(k) for k in ("renderer","driver","width","height","vsync")), "Incomplete display observation")
    return {"id":own_id,"rows":rows,"contacts":contacts,"deployment":deployment,"device":device,
            "timing":{"intervals":len(dt),"medianMs":statistics.median(dt),"p95Ms":percentile(dt,.95),
                      "p99Ms":percentile(dt,.99)}}


def qualify(plan, root):
    require(plan.get("schemaVersion")==1 and plan.get("transport") in ("private-loopback","real-network"),
            "Expected a version 1 two-endpoint scenario profile")
    require(type(plan.get("minimumAvailableThings")) is int and 0<=plan["minimumAvailableThings"]<=1048576
            and type(plan.get("p95BudgetMs")) in (int,float) and 0<plan["p95BudgetMs"]<=1000
            and isinstance(plan.get("thrownTemplates"),list) and 1<=len(plan["thrownTemplates"])<=32
            and all(isinstance(t,str) and 0<len(t)<=32 for t in plan["thrownTemplates"]), "Invalid authored scenario limits")
    roles=("host","guest");evidence=Evidence(root)
    peers={role:read_endpoint(plan["endpoints"][role],role,evidence) for role in roles}
    a,b=(peers[k]["deployment"] for k in roles)
    require(a["processId"] != b["processId"] or plan["transport"]=="real-network","Loopback requires two independent processes")
    require(all(a[k]==b[k] for k in ("world","packageSha256","engineSha256","game","target","transport"))
            and a["transport"]==plan["transport"], "Package, world, game, engine or transport differs between endpoints")
    ids={role:peers[role]["id"] for role in roles}
    require(len(set(ids.values()))==2,"Independent endpoints share the same local player")
    observations={}
    def check(name, passed, **details):
        observations[name]={"passed":bool(passed),**details}
    actions=lines(evidence.read(plan["actions"],128<<10),128)
    timestamps={}
    last={role:-1 for role in roles}
    for action in actions:
        key=(action["endpoint"],action["action"]);require(key not in timestamps,"Repeated scenario action")
        t=action["observedMs"]
        require(all(type(t[k]) is int and last[k]<=t[k] for k in roles),"Action clocks run backwards")
        last=t;timestamps[key]=t
    def interval(role, start, end):
        return [r for r in peers[role]["rows"] if timestamps[start][role]<=r["ms"]<=timestamps[end][role]]
    both={}
    for role in roles:
        both[role]=next((r for r in peers[role]["rows"] if {p["id"] for p in r["players"]}==set(ids.values())),None)
    require(all(both.values()),"Two connected participants were not observed at each endpoint")
    host=peers["host"]["rows"]
    alone=[r for r in host if r["ms"]<both["host"]["ms"] and len(r["players"])==1 and r["players"][0]["id"]==ids["host"]]
    check("lateJoin",bool(alone) and both["host"]["ms"]-alone[0]["ms"]>=500,
          hostSoloMs=both["host"]["ms"]-alone[0]["ms"] if alone else 0)
    starts=plan["starts"]
    require(isinstance(starts,list) and 2<=len(starts)<=32,"Supply bounded authored start volumes")
    for box in starts:
        require(type(box["sector"]) is int and len(box["min"])==len(box["max"])==3 and all(
            math.isfinite(v) for v in (*box["min"],*box["max"])) and all(i<=j for i,j in zip(box["min"],box["max"])),
            "Invalid authored start volume")
    # Allow initial network replication to settle before the first ordinary input.
    for role in roles:
        ms=timestamps[("host","walk")][role]
        both[role]=next((r for r in reversed(peers[role]["rows"]) if r["ms"]<=ms and len(r["players"])==2),both[role])
    used=[]
    for role,player_id in ids.items():
        pos=[actor(both[k],player_id) for k in roles]
        regions=[i for i,box in enumerate(starts) if all(p["sector"]==box["sector"] and p["health"]>0 and
                 all(lo<=x<=hi for lo,x,hi in zip(box["min"],p["position"],box["max"])) for p in pos)]
        check(role+"Spawn",len(regions)==1 and math.dist(pos[0]["position"],pos[1]["position"])<=.03,
              sector=pos[0]["sector"],positions=[p["position"] for p in pos],startRegions=regions)
        used.extend(regions)
    check("distinctStarts",len(set(used))==2)
    for role,other in (("host","guest"),("guest","host")):
        movement_result=movement(interval(role,(role,"walk"),(role,"stop-walk")),
                                 interval(other,(role,"walk"),(role,"stop-walk")),ids[role])
        observations[role+"Movement"]=movement_result
        impacts={}
        for viewer in roles:
            t0=timestamps[(role,"fire")][viewer];t1=timestamps[(role,"stop-fire")][viewer]
            shooter=actor(both[viewer],ids[role])["idThing"];victim=actor(both[viewer],ids[other])["idThing"]
            impacts[viewer]=sorted({e["template"] for e in peers[viewer]["contacts"] if t0<=e["ms"]<=t1
                                    and e["ownerThing"]==shooter and e["otherThing"]==victim and e["otherType"]==10})
        common=sorted(set(impacts["host"])&set(impacts["guest"]))
        check(role+"ProjectileContact",bool(common),observedTemplates=impacts)
    interruption = ("network","break-sync") if ("network","break-sync") in timestamps else ("network","disconnect-loopback")
    def after_join(role):
        return [r for r in peers[role]["rows"] if both[role]["ms"]<=r["ms"]<=timestamps[interruption][role]]
    pickup_flags={}
    for role in roles:
        states=after_join(role)
        pickup_flags[role]=[]
        for key in ("pickupDisabled","secondPickupDisabled"):
            disabled=False
            for row in states:
                if row.get(key) is True:
                    disabled=True
                elif disabled and row.get(key) is False:
                    pickup_flags[role].append(key)
                    break
    check("pickupAndReturn",bool(set(pickup_flags["host"])&set(pickup_flags["guest"])),observed=pickup_flags)
    lift={k:[min(r["liftZ"] for r in after_join(k)),max(r["liftZ"] for r in after_join(k))] for k in roles}
    check("stockLift",all(high-low>.3 for low,high in lift.values())
          and max(abs(i-j) for i,j in zip(lift["host"],lift["guest"]))<=.06, heights=lift)
    dead={}
    for viewer in roles:
        death=interval(viewer,("host","sustained-fire"),("guest","respawn"))
        dead[viewer]=max(actor(r,ids["guest"])["dead"] for r in death)
    check("guestDeath",all(v>0 for v in dead.values()),deathCounts=dead)
    respawn=interval("guest",("guest","respawn"),("guest","stop-respawn-walk"))
    motion=movement(interval("guest",("guest","respawn-walk"),("guest","stop-respawn-walk")),
                    interval("host",("guest","respawn-walk"),("guest","stop-respawn-walk")),ids["guest"])
    check("guestRespawn",all(v>0 for v in dead.values()) and any(actor(r,ids["guest"])["health"]>0 for r in respawn)
          and motion["passed"],postRespawnMovement=motion)
    thrown={}
    for viewer in roles:
        t0=timestamps[("guest","throw")][viewer];t1=timestamps[("guest","stop-throw")][viewer]
        owner=actor(both[viewer],ids["guest"])["idThing"]
        thrown[viewer]=sorted({(e["template"],e["sector"],e["surface"],e["otherThing"]) for e in peers[viewer]["contacts"]
                              if t0<=e["ms"]<=t1 and e["ownerThing"]==owner and e["template"] in plan["thrownTemplates"]
                              and (e["surface"]>=0 or e["otherThing"]>=0)})
    check("thrownContact",bool(set(thrown["host"])&set(thrown["guest"])),observed=thrown)
    negative=movement(interval("host",("host","isolated-walk"),("host","stop-isolated-walk")),
                      interval("guest",("host","isolated-walk"),("host","stop-isolated-walk")),ids["host"])
    check("brokenSyncRejected",negative["movedLocally"] and not negative["passed"],synchronizationAssertion=negative)
    metrics={}
    for viewer in roles:
        peer=peers[viewer];rows=after_join(viewer)
        min_free=min(r["capacity"]-r["occupied"] for r in rows)
        metrics[viewer]={"deployment":peer["deployment"],"device":peer["device"],"timing":peer["timing"],
            "peakOccupied":max(r["occupied"] for r in rows),"allocatedCapacity":min(r["capacity"] for r in rows),
            "minimumAvailable":min_free,"peakProjectiles":max(r["projectiles"] for r in rows),
            "visibleSectorsMax":max(r["visibleSectors"] for r in rows),"frustumsMax":max(r["frustums"] for r in rows)}
        check(viewer+"Headroom",min_free>=plan["minimumAvailableThings"],minimumAvailable=min_free,
              configuredWarningFloor=plan["minimumAvailableThings"])
        check(viewer+"FrameBudget",peer["timing"]["p95Ms"]<=plan["p95BudgetMs"],p95Ms=peer["timing"]["p95Ms"],
              declaredBudgetMs=plan["p95BudgetMs"])
    return {"schemaVersion":1,"passed":all(o["passed"] for o in observations.values()),"transport":plan["transport"],
            "game":a["game"],"target":a["target"],"world":a["world"],"packageSha256":a["packageSha256"],
            "peers":metrics,"observations":observations,"limits":["Gameplay evidence covers this authored route, two players and target.",
            "Frame intervals measure this renderer. Visible-sector counts do not prove every silhouette or opening.",
            "A native thermal throw and a Force power require separate observations. Remote health is not treated as authoritative."]}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile",type=Path)
    parser.add_argument("--bind",type=Path,metavar="OBSERVATIONS",help="bind an authored scenario to a directory of host and guest observations")
    args=parser.parse_args()
    try:
        require(args.profile.stat().st_size<=65536,"Oversized profile")
        plan=decode(args.profile.read_bytes())
        if args.bind:
            def ref(relative):
                path=args.bind/relative
                require(path.resolve().is_relative_to(args.bind.resolve()) and path.is_file() and path.stat().st_size<=24<<20,
                        f"Missing or oversized observation: {relative}")
                with path.open("rb") as stream:
                    checksum=hashlib.sha256()
                    for chunk in iter(lambda:stream.read(65536),b""):
                        checksum.update(chunk)
                return {"path":relative,"sha256":checksum.hexdigest()}
            names={"deployment":"deployment.json","packageAfter":"package-sha256.txt","observations":"peer-observation.jsonl",
                   "target":"peer-target.txt","frames":"peer-frames.csv","contacts":"peer-contacts.jsonl"}
            plan["actions"]=ref("actions.jsonl")
            plan["endpoints"]={role:{k:ref(role+"/"+v) for k,v in names.items()} for role in ("host","guest")}
            print(json.dumps(plan,indent=2,allow_nan=False))
            return 0
        result=qualify(plan,args.profile.parent)
        print(json.dumps(result,indent=2,allow_nan=False))
        return 0 if result["passed"] else 1
    except (ValueError,OSError,KeyError,TypeError,StopIteration,IndexError) as error:
        parser.exit(2,f"Invalid peer profile: {error}\n")


if __name__=="__main__":
    raise SystemExit(main())
