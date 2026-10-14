#!/bin/sh
# 文件功能：驱动 ISPD 2019 评估，组合原始设计与布线结果，生成 DRC/连通报告并评分。
# method=a 导入裁剪后的路由 DEF；method=b 合并输入设计与输出 NETS 段。
# 【零基础阅读】输入原 DEF 保留布局/原设计，输出 DEF 提供待评的实际路由，两者要配合。
# method=a 取输出 NETS 做增量导入，method=b 合并成完整文本；随后 Tcl 运行检查。
# $变量 在 Shell 中取值，反引号获取命令输出，sed 选取相应 DEF 段。
# 临时报告路径供官方评分程序读取，-keep 控制清理，与算法重新布线不是同一动作。

if [ $# -eq 0 ] ; then
    echo "$0 -lef <input LEF> -guide <guide file> -idef <input DEF> -odef <routed DEF>"
    echo "example:"
    echo "$0 -lef sample/ispd19_sample.input.lef -guide sample/ispd19_sample.input.guide -idef sample/ispd19_sample.input.def -odef sample/ispd19_sample.output.def"
    exit
fi

INVS="innovus"
# inputLef/inputRg/inputDef/outputDef 为库/guide/原设计/详细结果，不能把两个 DEF 互换。
# method 选增量或合并导入；trimmedDef 裁剪输出 NETS 文件，combinedDef 合并文件，dbuMicron 单位尺度。
# prefix 报告名前缀，evalGeoRpt/evalConRpt/evalScoreRpt 报告路径，evalTcl 临时脚本，invsLog 工具日志名。
EVAL="./ispd19eval_bin"

command -v $INVS || {
    echo "$INVS is not available."
    exit 1
}

method='a'
keepLogs=0
prefix=
trimmedDef='eval.def'
combinedDef='comb.def'

#read arguments
while [ $# -gt 0 ] ; do
    case $1 in
        -lef)   shift ; inputLef=$1 ;;
        -guide) shift ; inputRg=$1 ;;
        -idef)  shift ; inputDef=$1 ;;
        -odef)  shift ; outputDef=$1 ;;
        -tdef)  shift ; trimmedDef=$1 ;;
        -cdef)  shift ; combinedDef=$1 ;;
        -prefix) shift ; prefix=$1 ;;
        -method) shift ; method=$1 ;;
        -keep) keepLogs=1 ;;
        *) echo "unknown : $1" ;;
    esac
    shift
done

evalGeoRpt="${prefix}eval.geo.rpt"
evalConRpt="${prefix}eval.con.rpt"
evalScoreRpt="${prefix}eval.score.rpt"
evalTcl="${prefix}eval.tcl"
invsLog="${prefix}eval"

if [ ! -f $inputLef ] ; then
    echo "ERROR: $inputLef not found"
    exit
elif [ ! -f $inputRg ] ; then
    echo "ERROR: $inputRg not found"
    exit
elif [ ! -f $inputDef ] ; then
    echo "ERROR: $inputDef not found"
    exit
elif [ ! -f $outputDef ] ; then
    echo "ERROR: $outputDef not found"
    exit
fi

if [ -f $evalTcl ] ; then
    echo "WARNING: tcl file exists, overwriting..."
fi

# 两种导入方式都保留原始设计信息，将输出结果中的普通线网布线交给 Innovus 检查。
if [ "$method" = 'a' ] ; then
    dbuMicron=`grep "UNITS DISTANCE MICRONS" $inputDef | cut -d' ' -f 4`
    echo 'VERSION 5.8 ;' > $trimmedDef
    echo 'DIVIDERCHAR "/" ;' >> $trimmedDef
    echo 'BUSBITCHARS "[]" ;' >> $trimmedDef
    echo "UNITS DISTANCE MICRONS $dbuMicron ;" >> $trimmedDef
    sed -n '/^[[:blank:]]*NETS[[:blank:]]*[1-9][0-9]*[[:blank:]]*;[[:blank:]]*$/,/^[[:blank:]]*END NETS[[:blank:]]*$/p' $outputDef >> $trimmedDef
elif [ "$method" = 'b' ] ; then
    sed -n '/^[[:blank:]]*VERSION[[:blank:]]*[0-9]\.[0-9][[:blank:]]*;[[:blank:]]*$/,/^END SPECIALNETS[[:blank:]]*$/p' $inputDef > $combinedDef
    sed -n '/^[[:blank:]]*NETS[[:blank:]]*[1-9][0-9]*[[:blank:]]*;[[:blank:]]*$/,/^[[:blank:]]*END NETS[[:blank:]]*$/p' $outputDef >> $combinedDef
    echo "END DESIGN" >> $combinedDef

else
    echo "ERROR: unknown evaluation method $method"
    exit
fi

echo "#evaluation script for ISPD 2019 contest" > $evalTcl
echo 'source ispd19eval.tcl' >> $evalTcl
if [ "$method" = 'a' ] ; then
    echo "evaluate $inputLef $inputDef $trimmedDef $evalGeoRpt $evalConRpt" >> $evalTcl
else
    echo "evaluate $inputLef $combinedDef {} $evalGeoRpt $evalConRpt" >> $evalTcl
fi

echo 'exit' >>$evalTcl

if [ "$keepLogs" = 0 ] ; then
    rm -f $evalGeoRpt
    rm -f $evalConRpt
fi

cmd="$INVS -init $evalTcl -log $invsLog.log -overwrite -nowin"
echo $cmd
$cmd

if [ "$keepLogs" = 0 ] ; then
    rm -f $evalTcl
    rm -f "$invsLog.log"
    rm -f "$invsLog.logv"
    rm -f "$invsLog.cmd"
    rm -f `basename "$outputDef.v"`
fi

cmd="$EVAL -lef $inputLef -def $outputDef -guide $inputRg -georpt $evalGeoRpt -conrpt $evalConRpt"
echo $cmd
$cmd | tee $evalScoreRpt
