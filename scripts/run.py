#!/usr/bin/env python3
# 文件功能：批量运行 benchmark 的全局布线、详细布线评估和 Innovus 查看流程，归档日志及结果。
# 依赖 run_base 的算例目录和执行模式；通常在 build.py 生成的 run 目录中执行。
# 【零基础阅读】这是批量实验的总脚本：选算例 -> 找输入路径 -> 准备日志目录 -> 执行步骤。
# route 生成 guide，evaluate 先详细布线再评分，view 用图形工具看结果。
# argparse 在模块顶层执行，所以导入本文件也会解析命令行；检查时只解析语法/AST。
# run_base 的 run 执行外部命令，|& tee 同时把终端输出保存为日志。

import argparse
import os
import datetime
from run_base import *
from visualize_heatmap import visualize

# constants
binary = 'route'

# argparse
parser = argparse.ArgumentParser()
parser.add_argument('benchmarks', choices=all_benchmarks.get_choices(), nargs='+', metavar='BENCHMARK',
                    help='Choices are ' + ', '.join(all_benchmarks.get_choices()))
parser.add_argument('-m', '--mode', choices=modes)
parser.add_argument('-s', '--steps', choices=['route', 'eval', 'view', 'nano'], nargs='+', default=['route'])
parser.add_argument('-p', '--benchmark_path')
parser.add_argument('-t', '--threads', type=int, default=8)
parser.add_argument('-i', '--rrr_iters', type=int)
parser.add_argument('-e', '--eval_all_rrr_iters', action='store_true')
parser.add_argument('-o', '--others', default='')
parser.add_argument('-l', '--log_dir')
args = parser.parse_args()
# args.benchmarks 算例选择、steps 要执行阶段、benchmark_path 输入根、threads 工具请求线程数。
# args.mode 调试前缀选择、others 追加命令文字、log_dir 输出根目录。
# rrr_iters/eval_all_rrr_iters 是保留的历史选项，当前 C++ 入口并没有对应参数实现。
cmd_suffix = args.others
if args.eval_all_rrr_iters:
    cmd_suffix += ' -rrrWriteEachIter 1'

if args.rrr_iters is not None:
    cmd_suffix += ' -rrrIters {}'.format(args.rrr_iters)

# seleted benchmarks
bms = all_benchmarks.get_selected(args.benchmarks)
bm_path = args.benchmark_path
if bm_path is None:
    bm_path = os.environ.get('BENCHMARK_PATH')
    if bm_path is None:
        print('Benchmark path is not specified.')
        quit()

# mode cmd_prefix
cmd_prefix = mode_prefixes[args.mode]
if args.mode == 'valgrind':
    print('Please make sure the binary is not compiled with static linking to avoid false alarm')

# run
if args.log_dir is None:
    now = datetime.datetime.now()
    log_dir = 'run{:02d}{:02d}'.format(now.month, now.day)
else:
    log_dir = args.log_dir

run('mkdir -p {}'.format(log_dir))
print('The following benchmarks will be ran: ', bms)


# 调用 route 生成 guide；若有热力图则绘图，若启用 gprof 则输出性能调用图。
def route():
    # guide_file 输出走廊，log_file 路由日志；binary='route' 为当前目录程序名。
    # bm 当前算例，file_name_prefix 无后缀输入路径，cmd_prefix 调试工具前缀，cmd_suffix 追加选项。
    # heatmap_name 资源表文件；bm_log_dir 当前算例输出目录；二者不是同一个路径。
    guide_file = '{0}/{1}.solution.guide'.format(bm_log_dir, bm.full_name)
    log_file = '{0}/{1}.log'.format(bm_log_dir, bm.full_name)

    run('{cmd_prefix} ./{0} -lef {1}.input.lef -def {1}.input.def -threads {2} -output {3} {5} |& tee {4}'.format(
        binary, file_name_prefix, args.threads, guide_file, log_file, cmd_suffix, cmd_prefix=cmd_prefix))

    if args.mode == 'gprof':
        run('gprof {} > {}.gprof'.format(binary, bm.full_name))
        run('./gprof2dot.py -s {0}.gprof | dot -Tpdf -o {0}.pdf'.format(bm.full_name))
    
    # visualize grid graph heatmap
    heatmap_name = 'heatmap.txt'
    if os.path.exists(heatmap_name):
        visualize(heatmap_name)
        run('rm {}'.format(heatmap_name))
        # update figs
        run('rm -rf {}/figs'.format(bm_log_dir))
        run('mv -f figs {}'.format(bm_log_dir))
        
    run('mv *.solution.guide* *.solution.def* *.gprof *.pdf {} 2>/dev/null'.format(bm_log_dir))


# 先用 Dr. CU 将 guide 细化为 DEF，再按算例年份调用官方评估脚本。
def evaluate():
    # sol_file 详细布线 DEF，dr_file='drcu' 详细布线程序，dr_log_file 其日志，log_file 官方评估日志。
    # bm_yy 由算例名字提取年份 18/19，决定输入选项 -def 或 -idef/-odef。
    # evaluate_once 闭包借用这些路径运行一次，不重新展开全部 bms。
    guide_file = '{0}/{1}.solution.guide'.format(bm_log_dir, bm.full_name)
    sol_file = '{0}/{1}.solution.def'.format(bm_log_dir, bm.full_name)
    dr_log_file = '{0}/{1}_dr_eval.log'.format(bm_log_dir, bm.full_name)
    log_file = '{0}/{1}_eval.log'.format(bm_log_dir, bm.full_name)
    dr_file = 'drcu'
    bm_yy = int(bm.full_name[4:6])
    run('cp eval/ispd{0}eval/ispd{0}eval* ./'.format(bm_yy))

    def evaluate_once():
        # drcu 产生具体导线/过孔 DEF；官方脚本/Innovus 评的是这个详细布线结果。
        run('./{0} -lef {1}.input.lef -def {1}.input.def -guide {2} -threads {3} -tat 2000000000 -output {4} |& tee {5}'.format(
            dr_file, file_name_prefix, guide_file, args.threads, sol_file, dr_log_file))
        # run('gdb --args ./drcu_debug -lef {1}.input.lef -def {1}.input.def -guide {2} -threads {3} -tat 2000000000 -output {4} |& tee {5}'.format(
        #     dr_file, file_name_prefix, guide_file, args.threads, sol_file, dr_log_file))

        if bm_yy == 18:
            run('./ispd{0}eval.sh -lef {1}.input.lef -guide {2} -def {3} -thread {4} |& tee {5}'.format(
                bm_yy, file_name_prefix, guide_file, sol_file, args.threads, log_file))
        else:
            run('./ispd{0}eval.sh -lef {1}.input.lef -guide {2} -idef {1}.input.def -odef {3} -thread {4} |& tee {5}'.format(
                bm_yy, file_name_prefix, guide_file, sol_file, args.threads, log_file))

    evaluate_once()

    if args.eval_all_rrr_iters:
        for i in range(args.rrr_iters):
            sol_file = '{}/iter{}_{}.solution.def'.format(bm_log_dir, i, bm.full_name)
            log_file = 'iter{}_{}_eval.log'.format(i, bm.full_name)
            evaluate_once()
    run('rm ispd{0}eval_bin ispd{0}eval.sh ispd{0}eval.tcl ispd{0}eval.w *.def.v eval.def'.format(bm_yy))
    run('mv innovus.* *.solution.def* eval.*.rpt {} 2>/dev/null'.format(bm_log_dir))


def view():
    # file 为临时 Tcl 写出句柄，用输入 LEF+本算例 routed DEF 启动 Innovus 图形查看。
    file = open('tmp.tcl', 'w')
    file.write('loadLefFile {}.input.lef\n'.format(file_name_prefix))
    file.write('loadDefFile {}/{}.solution.def\n'.format(bm_log_dir, bm.full_name))
    file.write('setMultiCpuUsage -localCpu {}\n'.format(args.threads))
    file.write('win\n')
    file.close()
    run('innovus -init tmp.tcl')
    run('rm innovus.* *.drc.rpt *.solution.def.v tmp.tcl')


# 逐算例设置输入文件前缀和独立结果目录，并按 --steps 执行所需流程。
for bm in bms:
    bm_log_dir = '{}/{}'.format(log_dir, bm.abbr_name)
    file_name_prefix = '{0}/iccad2019c/{1}/{1}'.format(bm_path, bm.full_name)

    run('mkdir -p {}'.format(bm_log_dir))
    if 'route' in args.steps:
        route()
    if 'eval' in args.steps:
        evaluate()
    if 'view' in args.steps:
        view()
