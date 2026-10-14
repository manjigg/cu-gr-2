#!/bin/sh
# 文件功能：驱动 ISPD 2018 详细布线评估，调用 Innovus 生成几何/连通报告，再调用评分程序。
# 输入 LEF、guide 和布线后 DEF；脚本生成临时 Tcl 并按 -keep 控制日志保留。
# 【零基础阅读】Shell 用 $变量 取值，$# 是参数个数，shift 把当前首参数消费掉。
# case 按 -lef/-guide/-def 分支保存文件名，先检查文件，再拼 Tcl 调用 Innovus。
# > 覆盖写文件，>> 追加，| tee 把评分输出同时送到终端和报告。
# 该流程检查详细布线结果，不能只传全局 guide 就得到全部制造规则评分。

if [ $# -eq 0 ] ; then
    echo "$0 -lef <input LEF> -guide <guide file> -def <routed DEF>"
    echo "example:"
    echo "$0 -lef sample/ispd18_sample.input.lef -guide sample/ispd18_sample.input.guide -def sample/ispd18_sample.output.def"
    exit
fi

INVS="innovus"
# INVS 检查工具名，EVAL 评分二进制；inputLef 库、inputRg guide、outputDef 详细布线结果。
# evalGeoRpt 几何报告、evalConRpt 连通报告、evalScoreRpt 分数报告、evalTcl 临时驱动脚本。
# keepLogs 控制清理，cmd 是已拼好的外部工具命令，不是检查计算结果本身。
EVAL="./ispd18eval_bin"

command -v $INVS || {
    echo "$INVS is not available."
    exit 1
}

keepLogs=0

# 先收集输入路径，后续检查文件存在并生成 Innovus 所需的临时启动脚本。
#read arguments
while [ $# -gt 0 ] ; do
    case $1 in
        -lef)   shift ; inputLef=$1 ;;
        -guide) shift ; inputRg=$1 ;;
        -def)   shift ; outputDef=$1 ;;
        -keep)  shift ; keepLogs=1 ;;
        *) echo "unknown : $1" ;;
    esac
    shift
done

evalGeoRpt='eval.geo.rpt'
evalConRpt='eval.con.rpt'
evalScoreRpt='eval.score.rpt'
evalTcl='eval.tcl'

if [ ! -f $inputLef ] ; then
    echo "ERROR: $inputLef not found"
    exit
elif [ ! -f $inputRg ] ; then
    echo "ERROR: $inputRg not found"
    exit
elif [ ! -f $outputDef ] ; then
    echo "ERROR: $outputDef not found"
    exit
fi

if [ -f $evalTcl ] ; then
    echo "WARNING: tcl file exists, overwriting..."
fi

echo "#evaluation script for ISPD 2018 contest" > $evalTcl
echo 'source ispd18eval.tcl' >> $evalTcl
echo "evaluate $inputLef $outputDef $evalGeoRpt $evalConRpt" >> $evalTcl
echo 'exit' >>$evalTcl

if [ "$keepLogs" = 0 ] ; then
    rm -f $evalGeoRpt
    rm -f $evalConRpt
fi

cmd="$INVS -init $evalTcl -log eval.log -overwrite -nowin"
echo $cmd
$cmd

if [ "$keepLogs" = 0 ] ; then
    rm $evalTcl
    rm 'eval.log'
    rm 'eval.logv'
    rm 'eval.cmd'
    rm `basename "$outputDef.v"`
fi

cmd="$EVAL -lef $inputLef -def $outputDef -guide $inputRg -georpt $evalGeoRpt -conrpt $evalConRpt"
echo $cmd
$cmd | tee $evalScoreRpt
