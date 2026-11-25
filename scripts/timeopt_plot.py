"""Plot the measured runtime comparison and AES quality sensitivity."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def table(path):
    with path.open() as handle:
        return list(csv.DictReader(handle,delimiter='\t'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    comparison=[r for r in table(args.directory/'comparison_native.tsv') if r['benchmark']!='TOTAL']
    variants=[r for r in table(args.directory/'parameter_summary.tsv')
              if r['benchmark']=='aes' and r['wirelength_um'] and r['hbt_count']]
    fig,axes=plt.subplots(1,2,figsize=(10,4),constrained_layout=True)
    palette=('#356079','#a46737')
    x=range(len(comparison))
    axes[0].bar([i-0.19 for i in x],[float(r['old_seconds']) for r in comparison],width=.38,
                color=palette[0],label='Native baseline')
    axes[0].bar([i+0.19 for i in x],[float(r['new_seconds']) for r in comparison],width=.38,
                color=palette[1],label='TimeOpt Native')
    axes[0].set_xticks(list(x),[r['benchmark'] for r in comparison],rotation=28,ha='right')
    axes[0].set(ylabel='Process wall time (s)',yscale='log')
    axes[0].legend(frameon=False)
    ax=axes[1]
    markers=(('beam-','o'),('hbt-','^'),('seed-','s'),('local-','D'))
    for prefix,marker in markers:
        batch=[r for r in variants if r['experiment'].split('/')[-1].startswith(prefix)]
        ax.scatter([int(r['hbt_count']) for r in batch],[float(r['wirelength_um']) for r in batch],
                   marker=marker,s=30,label=prefix.rstrip('-'))
    ax.set(xlabel='Physical HBT count (AES)',ylabel='Global wirelength (µm)')
    ax.legend(frameon=False)
    for axis in axes:
        axis.grid(axis='y',alpha=.2)
        axis.set_axisbelow(True)
    fig.savefig(args.directory/'performance.pdf')
