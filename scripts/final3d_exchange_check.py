"""Recheck recorded joint combinations against their fixed outside-resource snapshot."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path


def check_exchange(manifest, event):
    """Recompute the four recorded two-net transitions without the C++ selection routine."""
    sites = {s["id"]:s for s in manifest["bonding"]["sites"]}
    spacing = manifest["bonding"]["min_spacing"]
    by_id = {r["id"]:r for r in event["outside_resources"]}
    outside_sites = {r["site"] for r in by_id.values() if r["type"]==2 and r["outside"]>1e-12}
    costs = manifest["weights"]
    budget = event["hbt_budget"]

    def verify_route(net, route):
        vertices, via_count, seen_sites = {},0,set()
        for edge in route["edges"]:
            a,b = tuple(edge["from"]),tuple(edge["to"])
            if b in vertices.setdefault(a,set()) or a==b:
                raise ValueError("Repeated physical edge")
            vertices[a].add(b)
            vertices.setdefault(b,set()).add(a)
            if edge["type"]==2:
                site = sites[edge["site"]]
                if a[0]==b[0] or edge["site"] in seen_sites or site.get("fixed_net",net)!=net:
                    raise ValueError("Illegal recorded crossing")
                seen_sites.add(edge["site"])
            elif edge["type"]==1:
                if a[0]!=b[0] or a[2:]!=b[2:] or abs(a[1]-b[1])!=1:
                    raise ValueError("Illegal recorded ordinary via")
                via_count += 1
            elif edge["type"]!=0 or a[:2]!=b[:2] or sum(abs(a[i]-b[i]) for i in (2,3))!=1:
                raise ValueError("Illegal recorded wire segment")
        root = tuple(route["root"])
        vertices.setdefault(root,set())
        seen,queue = set(),[root]
        while queue:
            point = queue.pop()
            if point not in seen:
                seen.add(point)
                queue.extend(vertices[point]-seen)
        if len(seen)!=len(vertices) or len(vertices)!=len(route["edges"])+1:
            raise ValueError("Recorded candidate is disconnected or cyclic")
        if seen_sites!=set(route["hbt"]) or via_count!=route["via"]:
            raise ValueError("Recorded edges and crossing totals disagree")
        if any(s.get("fixed_net")==net and i not in seen_sites for i,s in sites.items()):
            raise ValueError("Missing compulsory fixed crossing")
        if not costs.get("short_area_um2"):
            price=costs["wire_um"]*route["wire_um"]+costs["via"]*via_count+costs["hbt"]*len(seen_sites)
            for rid,amount in route["demand"]:
                resource=by_id[rid]
                w=costs.get("hbt_pressure",costs["congestion"]) if resource["type"]==2 else costs["congestion"]
                price+=w*amount/(1+resource["capacity"])
            if not math.isclose(price,route["cost"],rel_tol=1e-12,abs_tol=1e-9):
                raise ValueError("Recorded fixed objective disagrees with demands")

    def feasible(routes):
        requested=Counter()
        used=set()
        for route in routes:
            for rid,value in route["demand"]:
                requested[rid]+=value
            for site in route["hbt"]:
                if site in used or site in outside_sites:
                    return False
                for other in used | outside_sites:
                    a,b=sites[site]["xy"],sites[other]["xy"]
                    if sum((x-y)**2 for x,y in zip(a,b))<spacing**2:
                        return False
                used.add(site)
        return all(value+by_id[rid]["outside"]<=by_id[rid]["capacity"]+1e-12 for rid,value in requested.items())

    def value(routes):
        hbts=event["outside_hbt"]+sum(len(r["hbt"]) for r in routes)
        return (0 if budget is None else max(0,hbts-budget)),math.fsum(r["cost"] for r in routes)

    def improves(new,old):
        a,b=value(new),value(old)
        return a[0]<b[0] or (a[0]==b[0] and a[1]+1e-9<b[1])

    before,after=event["before"],event["after"]
    for group in (before,after):
        for net,route in zip(event["nets"],group):
            verify_route(net,route)
    if not feasible(before) or not feasible(after) or not improves(after,before):
        raise ValueError("Accepted combination does not meet feasibility or improvement")
    result={"passed":True,"strict_pair":False,"delta_fixed_objective":value(after)[1]-value(before)[1]}
    if len(before)==2:
        seq=[]
        for i,side in enumerate(("first_A","first_B")):
            single=before.copy()
            single[i]=after[i]
            physical=feasible(single)
            accept=physical and improves(single,before)
            seq.append(accept and improves(after,single))
            calculated=dict(unilateral_physical=physical,unilateral_accepted=accept,sequence_accepted=seq[-1])
            if calculated!=event[side]:
                raise ValueError("Unilateral and ordered tests disagree with trace")
            result[side]=calculated
        differs=[(r["root"],r["edges"])!=(s["root"],s["edges"]) for r,s in zip(before,after)]
        result["strict_pair"]=all(differs) and not any(seq)
    if result["strict_pair"]!=event["strict_pair"]:
        raise ValueError("Strict pair classification disagrees with trace")
    return result


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest",type=Path)
    parser.add_argument("events",type=Path)
    args=parser.parse_args()
    config=json.loads(args.manifest.read_text())
    print(json.dumps([check_exchange(config,event) for event in json.loads(args.events.read_text())],indent=2))
