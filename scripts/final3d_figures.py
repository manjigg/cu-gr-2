"""Generate vector and raster paper figures from final3D tables and exchange evidence."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import subprocess

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Circle
from matplotlib.ticker import MultipleLocator


def rows(path):
    if not path.is_file():
        return []
    with path.open() as f:
        return list(csv.DictReader(f,delimiter="\t"))


def number(text):
    return None if text in (None,"","N/A","NA") else float(text)


class Figures:
    def __init__(self, global_dir, detail_dir):
        self.global_dir,self.detail_dir=global_dir,detail_dir
        for folder in [global_dir,detail_dir]:
            (folder/"figures").mkdir(parents=True,exist_ok=True)
        plt.rcParams.update({"figure.dpi":135,"font.size":9,"axes.spines.top":False,
                             "axes.spines.right":False,"svg.fonttype":"none","pdf.fonttype":42})

    def save(self, fig, name, folder=None, layout_rect=None):
        fig.tight_layout(rect=layout_rect)
        for suffix in ("pdf","svg","png"):
            fig.savefig((folder or self.global_dir)/"figures"/(name+"."+suffix),bbox_inches="tight")
        plt.close(fig)

    def diagram(self, name, labels):
        directory=self.global_dir/"figures"
        source=directory/(name+".dot")
        sequence=" -> ".join(f"n{i}" for i in range(len(labels)))
        nodes="\n".join(f"n{i} [label={json.dumps(label)}];" for i,label in enumerate(labels))
        source.write_text("digraph workflow {\nrankdir=LR; bgcolor=white;\n"
                          "node [shape=box, style=\"rounded,filled\", color=\"#b8d1e6\","
                          " fillcolor=\"#edf6ff\", fontname=Helvetica, margin=0.12];\n"
                          "edge [color=\"#496987\"];\n"+nodes+"\n"+sequence+";\n}\n")
        for suffix in ("pdf","svg","png"):
            subprocess.run(["dot","-T"+suffix,source,"-o",directory/(name+"."+suffix)],check=True)

    def schematics(self):
        self.diagram("routing-flow",["Import dual-die layout","Build Wire, Via and HBT graph",
                     "Stage 1\nSeed routing","Stage 2\nArchive + joint exchange","Stage 3\nOriginal maze refinement",
                     "Independent replay\nand detailed routing"])
        self.diagram("candidate-archive",["True endpoint tree","Static resource checks",
                     "Physical edge deduplication","Resource dominance","Bounded diverse alternatives","Joint pool"])
        self.diagram("resource-conflict",["Net B requests r","Capacity / HBT spacing check","Active owner index",
                     "Observe Net A occupies r or adjacent HBT","Queue physical exchange (A, B)"])
        self.diagram("group-read-view",["Live demand","Subtract old demand of A and B",
                     "Keep all fixed rules and outside use","Regenerate shared-group candidates",
                     "Check summed demand before commit"])
        self.diagram("finite-search",["Incumbent + per-net alternatives","Branch by candidate",
                     "Fixed-cost / HBT lower bounds","Validate complete combination",
                     "Atomic publish only if improving"])
        fig,axes=plt.subplots(1,3,figsize=(8,2.5))
        panels=[
            ("Merge",[(1,0),(3,0)],[(1,1)],[(1,1),(3,1)]),
            ("Relocate",[(1,0)],[(3,1)],[(1,1),(3,1)]),
            ("Augment",[(1,0)],[(1,1),(3,1),(4,1)],[(1,1),(3,1),(4,1)]),
        ]
        for ax,(title,old,new,points) in zip(axes,panels):
            for y in [0,1]:
                ax.plot([0,5],[y,y],linewidth=2,color="#b8d1e6")
            for x,_ in old:
                ax.plot([x,x],[0,1],linestyle="--",color="#bc5741",linewidth=1.5)
            for x,_ in new:
                ax.plot([x,x],[0,1],color="#246d98",linewidth=2.2)
            for x,y in points:
                ax.add_patch(Circle((x,y),0.078,color="#246d98"))
            ax.set_xlim(0,5)
            ax.set_ylim(-0.2,1.35)
            ax.set_title(title)
            ax.axis("off")
        fig.suptitle("HBT topology operations (schematic)    dashed: before    solid: after")
        self.save(fig,"hbt-operations")

    def comparisons(self):
        for group in (self.global_dir,self.detail_dir):
            data=[r for r in rows(group/"comparison_v3.tsv") if number(r["WL变化"]) is not None
                  and not r["Benchmark"].startswith("合计")]
            if not data:
                continue
            fig,axes=plt.subplots(2,2,figsize=(8.2,5.0),sharex=True)
            for ax,key,label in zip(axes.flat,["WL变化","Via变化","HBT变化","RT变化"],
                                    ["Wire length","Ordinary vias","HBTs","Routing time"]):
                ax.bar([r["Benchmark"] for r in data],[float(r[key]) for r in data],color="#2c769b")
                ax.axhline(0,color="#35495a",linewidth=.8)
                ax.set_title(label)
                ax.set_ylabel("Change (%)")
                ax.tick_params(axis="x",rotation=30)
            self.save(fig,"comparison-v3",group)
        old=rows(self.global_dir/"baseline/summary.tsv")
        new=rows(self.global_dir/"summary.tsv")
        jobs=[r for r in new if r["status"]=="PASS" and number(r["max_rss_kb"]) is not None]
        if jobs:
            fig,ax=plt.subplots(figsize=(7.6,3.2))
            reference={r["benchmark"]:number(r["max_rss_kb"]) for r in old}
            x=list(range(len(jobs)))
            ax.bar([i-.18 for i in x],[reference[r["benchmark"]]/1024 for r in jobs],.36,label="V3",color="#9db9cc")
            ax.bar([i+.18 for i in x],[number(r["max_rss_kb"])/1024 for r in jobs],.36,label="Final",color="#2c769b")
            ax.set_xticks(x,[r["benchmark"] for r in jobs],rotation=25)
            ax.set_ylabel("Peak process RSS (MiB)")
            ax.legend()
            self.save(fig,"peak-memory")

    def ablations(self):
        pairs=[("ablation_summary.tsv","ablation-objective"),("exchange/same_stage2_summary.tsv","same-seed-stage2")]
        for file,name in pairs:
            data=[r for r in rows(self.global_dir/file) if r["status"]=="PASS"]
            groups=defaultdict(dict)
            for r in data:
                groups[r["benchmark"]][r["configuration"]]=r
            if not any("A0" in g and len(g)>1 for g in groups.values()):
                continue
            fig,ax=plt.subplots(figsize=(7.5,3.6))
            for case,group in groups.items():
                if "A0" not in group:
                    continue
                base=number(group["A0"]["best_feasible_objective"])
                selected=[(label,number(r["best_feasible_objective"])) for label,r in sorted(group.items())]
                ax.plot([n for n,v in selected if v is not None],[100*(v/base-1) for _,v in selected if v is not None],
                        marker="o",markersize=3,label=case)
            ax.axhline(0,color="#35495a",linewidth=.8)
            ax.set_ylabel("Fixed objective change (%)")
            ax.legend()
            self.save(fig,name)

    def tradeoff(self):
        groups=defaultdict(list)
        for r in rows(self.global_dir/"hbt_tradeoff/summary.tsv"):
            if r["budget_feasible"]=="True" and r["independent_check"]=="passed":
                groups[(r["benchmark"],r["version"])].append(r)
        if not groups:
            return
        fig,axes=plt.subplots(2,2,figsize=(9,6))
        variables=[("hbt_budget","wirelength_um","Wire length (µm)"),("hbt_budget","via_count","Ordinary vias"),
                   ("used_hbt","wirelength_um","Wire length (µm)"),("hbt_budget","wall_seconds","Wall time (s)")]
        for idx,ax in enumerate(axes.flat):
            x,y,label=variables[idx]
            for (case,version),data in sorted(groups.items()):
                ordered=sorted(data,key=lambda p:number(p[x]))
                ax.plot([number(p[x]) for p in ordered],[number(p[y]) for p in ordered],
                        "-o" if version=="final" else "--s",markersize=3,label=case+" "+version)
            ax.set_xlabel("HBT budget" if x=="hbt_budget" else "Actual HBT count")
            ax.set_ylabel(label)
        axes[0,0].legend(fontsize=7,ncol=2)
        self.save(fig,"hbt-budget-tradeoff")

    def timing(self):
        records=rows(self.global_dir/"runtime_breakdown.tsv")
        stages=["stage_1","stage_2","stage_3","guide_output"]
        data=defaultdict(dict)
        for r in records:
            if r["module"] in stages:
                data[(r["benchmark"],r["version"])][r["module"]]=number(r["inclusive_seconds"])
        if data:
            fig,ax=plt.subplots(figsize=(9.1,3.2))
            labels=sorted(data)
            cumulative=[0.]*len(labels)
            for mod,color in zip(stages,["#367094","#67a1bb","#a6c9d5","#cc8845"]):
                values=[data[k].get(mod,0.) for k in labels]
                ax.bar(range(len(labels)),values,bottom=cumulative,label=mod.replace("_"," ").title(),color=color)
                cumulative=[old+inc for old,inc in zip(cumulative,values)]
            ax.set_xticks(range(len(labels)),[" ".join(x) for x in labels],rotation=35,ha="right")
            ax.set_ylabel("Profiled stage wall time (s)")
            ax.legend()
            self.save(fig,"routing-stage-profile")
        data=[r for r in rows(self.global_dir/"ablation_summary.tsv")
              if r["status"]=="PASS" and number(r["exchange_runtime_seconds"]) is not None]
        if data:
            fig,ax=plt.subplots(figsize=(6,3.6))
            for case in dict.fromkeys(r["benchmark"] for r in data):
                p=[r for r in data if r["benchmark"]==case]
                ax.scatter([number(r["exchange_runtime_seconds"]) for r in p],
                           [-number(r["exchange_delta_objective"]) for r in p],label=case)
            ax.set_xlabel("Exchange runtime (s)")
            ax.set_ylabel("Accepted fixed objective reduction")
            ax.legend()
            self.save(fig,"joint-overhead-and-improvement")

    def real_exchange(self):
        cases=rows(self.global_dir/"exchange/real_cases.tsv")
        selected=next((r for r in cases if r["strict_pair"]=="True" and r["actual_blockers"]!="0"
                       and r["independent_check"]=="PASS"),None)
        if not selected:
            return
        event=json.loads(Path(selected["evidence"]).read_text())[int(selected["case"])-1]
        colors=["#2c769b","#bc5741"]
        fig,axes=plt.subplots(1,4,figsize=(10,3.8),sharex=True,sharey=True)
        for index,mode in enumerate(["before","after"]):
            for die in (0,1):
                ax=axes[2*index+die]
                for i,route in enumerate(event[mode]):
                    for edge in route["edges"]:
                        a,b=edge["from"],edge["to"]
                        if edge["type"]==2 and (a[0]==die or b[0]==die):
                            p=a if a[0]==die else b
                            ax.scatter(p[2],p[3],marker="D",s=20,color=colors[i],zorder=3)
                            ax.annotate(str(edge["site"]),p[2:],fontsize=7,xytext=(3,1),textcoords="offset points")
                        elif a[0]==b[0]==die and edge["type"]==0:
                            ax.plot([a[2],b[2]],[a[3],b[3]],color=colors[i],linewidth=1.2)
                ax.set_title(f"{mode.title()} · {('top','bottom')[die]} die")
                ax.set_aspect("equal",adjustable="box")
                ax.xaxis.set_major_locator(MultipleLocator(1))
                ax.yaxis.set_major_locator(MultipleLocator(1))
        axes[0].set_ylabel("Local GCell y")
        fig.supxlabel("Local GCell x",y=.091,fontsize=9)
        ids=selected["nets"].split(",")
        handles=[Line2D([],[],color=c,label="Net "+n) for c,n in zip(colors,ids)]
        handles.append(Line2D([],[],color="#35495a",marker="D",linestyle="None",label="HBT site"))
        fig.legend(handles=handles,loc="lower center",ncol=3,frameon=False)
        fig.suptitle(selected["benchmark"]+" physical HBT exchange")
        self.save(fig,"observed-joint-topology",layout_rect=(0,.13,1,1))


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--global-dir",type=Path,default=Path("result-3/final3D"))
    parser.add_argument("--detail-dir",type=Path,default=Path("result-3/final3Ddetail"))
    args=parser.parse_args()
    figures=Figures(args.global_dir,args.detail_dir)
    for method in (figures.schematics,figures.comparisons,figures.ablations,figures.tradeoff,figures.timing,
                   figures.real_exchange):
        method()
