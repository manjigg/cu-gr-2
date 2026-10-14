# 文件功能：提供 Bash 命令执行助手、调试模式前缀及 benchmark 名称/别名/集合管理。
# Benchmarks 将命令行选择（单个算例、集合或 all）展开成 Benchmark 对象列表。
# 【零基础阅读】这是 run.py 导入的助手，列表存算例，字典存集合名字与成员。
# self 是当前对象，__init__ 像 C++ 构造函数，创建对象时设置它自己的名单。
# get_selected 将简写/全名/集合展开成待运行列表；run 用 Bash 执行拼好的命令。
# 算例名单只是输入选择，没有在这里打开每个 LEF/DEF，也没有计算路线。
import os

# run command


def run(command):
    print(command)
    os.system('/bin/bash -c \'{0}\''.format(command))


# run modes
modes = ['gdb', 'valgrind', 'vgdb', 'gprof']
mode_prefixes = {
    None: '',
    'gprof': '',
    'gdb': 'gdb --args',
    'valgrind': 'valgrind',
    'vgdb': 'valgrind --vgdb-error=0'
}

# benchmarks


class Benchmark:
    # full_name 磁盘输入目录用的算例全名，abbr_name 命令行便利简写；两者不是输入路径本身。
    def __init__(self, full_name, abbr_name):
        self.full_name = full_name
        self.abbr_name = abbr_name

    def __repr__(self):
        return self.full_name


# 管理全名、简写和算例集合；列表末尾的设置是当前项目支持的 benchmark 清单。
class Benchmarks:
    # self.__bms 全部 Benchmark 列表，self.__bm_sets：“集合名->Benchmark 列表”字典。
    # ids 是型号模式中要填的序号文字；name 当前查找名；bms 当前展开结果，selected 最终待办。
    def __init__(self):
        self.__bms = []
        self.__bm_sets = dict()

    def add(self, full_name_pat, abbr_name_pat, ids):
        for id in ids:
            self.__bms.append(
                Benchmark(full_name_pat.format(id), abbr_name_pat.format(id)))

    def get_bm(self, name):
        # TODO: can be faster by using hash table
        for bm in self.__bms:
            if name == bm.abbr_name or name == bm.full_name:
                return bm
        print('Error: benchmark', name, 'cannot be found')
        quit()

    def add_set(self, set_name, bm_names):
        bms = []
        for name in bm_names:
            bms.append(self.get_bm(name))
        self.__bm_sets[set_name] = bms

    def get_choices(self):
        choices = []
        for bm in self.__bms:
            choices.append(bm.full_name)
            choices.append(bm.abbr_name)
        for bm_set in self.__bm_sets:
            choices.append(bm_set)
        choices.append('all')
        return choices

    def get_selected(self, names):
        # 选 all 直接返回全部算例；否则逐项看是集合还是单个算例，再拼接结果。
        if 'all' in names:
            return self.__bms
        else:
            selected = []
            for name in names:
                if name in self.__bm_sets:
                    selected += self.__bm_sets[name]
                else:
                    selected.append(self.get_bm(name))
            return selected


######################################
# Below is project-dependant setting #
######################################


# all benchmarks
all_benchmarks = Benchmarks()
all_benchmarks.add('ispd18_test{}', '8t{}', ['1', '2', '3', '4', '5', '6', '7', '8', '9', '10'])
all_benchmarks.add('ispd19_test{}', '9t{}', ['1', '2', '3', '4', '5', '6', '7', '8', '9', '10'])
all_benchmarks.add('ispd18_test{}_metal5', '8t{}m', ['5', '8', '10'])
all_benchmarks.add('ispd19_test{}_metal5', '9t{}m', ['7', '8', '9'])

all_benchmarks.add_set('ispd18' , ['8t1', '8t2', '8t3', '8t4', '8t5', '8t6', '8t7', '8t8', '8t9', '8t10'])
all_benchmarks.add_set('ispd19' , ['9t1', '9t2', '9t3', '9t4', '9t5', '9t6', '9t7', '9t8', '9t9', '9t10'])
all_benchmarks.add_set('iccad19', ['8t5', '8t8', '8t10', '9t7', '9t8', '9t9', '8t5m', '8t8m', '8t10m', '9t7m', '9t8m', '9t9m'])
