#!/usr/bin/env python
# 文件功能：解析构建模式，调用 CMake 编译 route，并将二进制、运行脚本和 FLUTE 表复制到 run。
# 从项目根目录运行；build 保存中间产物，run 保存执行布线所需的文件。
# 【零基础阅读】Python 缩进决定代码属于哪个函数/分支，不用 C++ 的花括号。
# argparse 将 -o release 等命令行文字填进 args；字典把模式名映射到 CMake 参数。
# run 把文字命令交给系统执行，format 用变量填命令中的占位符。
# CMake 配置 -> 准备运行目录 -> 编译 -> 复制 route/脚本/查找表，顺序应完整理解。

import argparse, os

all_targets = ['route']
# all_targets 可构建程序名；run_files 待复制脚本/评估/二进制清单，不是源码编译清单。
run_files = 'scripts/*.py eval drcu'

# 执行构建命令，失败时结束；-p 控制是否在执行前打印命令，并非仅预览。
def run(command):
    # command 已拼好的 Shell 命令文字；os.system 返回退出状态，失败则结束本构建流程。
    if args.print_commands:
        print(command)
    if os.system(command) is not 0:
        if not args.print_commands:
            print(command)
        quit()

# cmake & copy run files
# for sanitize, need to remove -static from src/CMakeLists.txt
# 不同调试/性能分析模式对应的 CMake 参数；消毒器模式受现有静态链接设置影响。
mode_cmake_options = {
    None                : '',
    'debug'             : '-DCMAKE_BUILD_TYPE=Debug',
    'release'           : '-DCMAKE_BUILD_TYPE=Release',
    'profile'           : '-DCMAKE_CXX_FLAGS=-pg',
    'release_profile'   : '-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-pg',
    'asan'              : '-DCMAKE_CXX_FLAGS=-fsanitize=address',
    'debug_asan'        : '-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=address',
    'tsan'              : '-DCMAKE_CXX_FLAGS=-fsanitize=thread',
    'debug_tsan'        : '-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=thread'
}

# argparse
parser = argparse.ArgumentParser(description='Build CUGR in Linux')
parser.add_argument('-t', '--targets', choices=all_targets, nargs='+')
parser.add_argument('-o', '--mode', choices=mode_cmake_options.keys())
parser.add_argument('-c', '--cmake_options', default='')
parser.add_argument('-m', '--make_options', default='-j 6')
parser.add_argument('-p', '--print_commands', action='store_true')
parser.add_argument('-b', '--build_dir', default='build')
parser.add_argument('-r', '--run_dir', default='run')
args = parser.parse_args()
# args.mode 模式、targets 指定目标、cmake_options/make_options 附加选项。
# args.build_dir 编译产物目录、run_dir 执行目录、print_commands 是否打印命令（仍执行）。

# targets
if args.targets is None:
    build_targets = ['']
else:
    build_targets = args.targets

# -B 选择构建产物目录；此行只生成规则，下面 cmake --build 才调用实际编译。
run('cmake src -B {} {} {}'.format(args.build_dir, mode_cmake_options[args.mode], args.cmake_options))
run('mkdir -p {}'.format(args.run_dir))
run('cp -u -R {} {}'.format(run_files, args.run_dir))

# make
for target in build_targets:
    # target 本轮程序目标，build_targets 空目标字符串表示默认全部构建；cp_targets 为实际复制名。
    run('cmake --build {} --target {} -- {}'.format(args.build_dir, target, args.make_options))
cp_targets = all_targets if build_targets == [''] else build_targets
for target in cp_targets:
    run('cp -u {}/{} {}'.format(args.build_dir, target, args.run_dir))

# flute LUT
# FLUTE 运行时按当前目录找数据表，复制表与复制 route 都是准备执行环境。
run('cp flute/*.dat {}'.format(args.run_dir))
