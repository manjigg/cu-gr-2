"""Plot the nondominated measured wire-length/HBT pairs from a native weight sweep."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    args=argparse.ArgumentParser(description=__doc__)
    args.add_argument('table',type=Path)
    args.add_argument('plot',type=Path)
    args.add_argument('--title',default='Native CUGR-3D')
    a=args.parse_args()
    with a.table.open() as stream:
        rows=[r for r in csv.DictReader(stream,delimiter='\t') if r['wirelength_um']]
    points=sorted({(int(r['used_hbt']),float(r['wirelength_um'])) for r in rows})
    front=[]
    for x,y in points:
        if (not front or y<front[-1][1]) and (not front or x!=front[-1][0]):
            front.append((x,y))
    fig,ax=plt.subplots(figsize=(6,3.8),constrained_layout=True)
    ax.scatter(*zip(*points),s=40,color='#64748b',label='Measured settings')
    ax.plot(*zip(*front),marker='o',color='#006b65',linewidth=1.7,label='Nondominated measurements')
    ax.set(xlabel='Distinct physical HBT sites',ylabel='Global wirelength (µm)',title=a.title)
    ax.ticklabel_format(axis='y',style='plain',useOffset=False)
    ax.grid(axis='y',alpha=.22)
    ax.legend(frameon=False)
    fig.savefig(a.plot,dpi=180)

if __name__=='__main__':
    main()
