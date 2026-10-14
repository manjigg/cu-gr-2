#!/usr/bin/env python3
# 文件功能：读取 GridGraph::write 输出的逐层资源余额，绘制并保存每层的 PDF 热力图。
# 输入首行为层数/x尺寸/y尺寸，随后每层名称和 y 行 x 列数据；由 run.py 调用 visualize。
# 【零基础阅读】先按文件格式读数字，再给每层建立一个 y 行/x 列矩阵供 imshow 显示。
# 正余额表示仍有空间，负余额表示挤满；颜色范围固定，图的横纵轴是格子位置。
# figsize 决定图大小，invert_yaxis 修正显示方向，savefig 写 PDF 不改变原路由结果。
# 本文件提供 visualize 函数，运行脚本调用它；文件本身没有独立命令行入口。
import argparse
import matplotlib.pyplot as plt
import math
import os



def visualize(heatmap_name):
    # heatmap_name 输入路径，file 读取流，line 当前分词行；n_layers/x_size/y_size 输入维度。
    # layer_names[层] 名字，heatmaps[层][y][x] 余额，heatmap 当前层矩阵，layer_idx 当前层编号。
    # v_max 颜色极值，min_figsize 最小画布大小，scale 每格画布缩放，fig_directory PDF 目录。
    # fig 当前图、ax 主坐标轴、cax 色条坐标轴；n_cols/n_rows 当前没有用于启用的 subplot。
    print('visializing heatmap...')
    file = open(heatmap_name, 'r')
    
    # 正数表示剩余容量，负数表示拥塞；图中统一使用 [-10,10] 的颜色范围。
    line = file.readline().split(' ')
    n_layers = int(line[0]);
    x_size = int(line[1]);
    y_size = int(line[2]);
    
    n_cols = 4
    n_rows = math.ceil(n_layers / n_cols)
    v_max = 10.0
    
    layer_names = []
    heatmaps = []
    
    for layer_idx in range(n_layers):
        layer_name = file.readline()
        heatmap = [[0.0 for x in range(x_size)] for y in range(y_size)]
        for y in range(y_size):
            line = file.readline().split(' ')
            for x in range(x_size):
                heatmap[y][x] = float(line[x])
        layer_names.append(layer_name)
        heatmaps.append(heatmap)
    
    fig_directory = 'figs'
    os.system('mkdir -p {}'.format(fig_directory))
    
    min_figsize = 10
    scale = 0.02
    for layer_idx in range(n_layers):
        fig = plt.figure(
            figsize=(
                max(x_size * scale, min_figsize), 
                max(y_size * scale, min_figsize)
            )
        )
        # plt.subplot(n_rows, n_cols, layer_idx + 1)
        plt.imshow(heatmaps[layer_idx], vmin=-v_max, vmax=v_max, cmap='coolwarm_r')
        plt.title(layer_names[layer_idx])
        
        ax = plt.gca()
        ax.invert_yaxis()
        cax = fig.add_axes([ax.get_position().x1 + 0.005, ax.get_position().y0, 0.005, ax.get_position().height])
        plt.colorbar(cax=cax)
        
        plt.savefig('{}/heatmap_{}.pdf'.format(fig_directory, layer_idx))
                
