
# 文件功能：在 Innovus 中加载 LEF/布线 DEF，设置 ISPD 2018 检查规则并写出几何及连通报告。
# evaluate 是 Shell 评估脚本的调用入口；catch 捕获工具命令错误以继续生成报告。
# 【零基础阅读】Tcl 的 proc 定义过程，$name 取变量，花括号组织参数/命令块。
# loadDesign 加载库与具体布局，setVerifyGeometryMode 设置本次要检查哪些规则。
# catch 调用工具命令并捕获错误，报告名字由 Shell 传进来以供评分程序继续使用。
# 这些命令属于 Innovus 环境，普通 Tcl 解释器只可检查文本完整性，不能完成芯片 DRC。

proc evaluate { inputLef outputDef evalGeoRpt evalConRpt } {
    # 四个参数依次为库、详细布线 DEF、几何报告路径、连通报告路径，不是文件内容。
    setMultiCpuUsage -localCpu 8
    loadDesign $inputLef $outputDef

    setVerifyGeometryMode -area { 0 0 0 0 }
    setVerifyGeometryMode -minWidth false
    setVerifyGeometryMode -minSpacing true
    setVerifyGeometryMode -minArea true
    setVerifyGeometryMode -sameNet true
    setVerifyGeometryMode -short true
    setVerifyGeometryMode -overlap false
    setVerifyGeometryMode -offRGrid false
    setVerifyGeometryMode -offMGrid false
    setVerifyGeometryMode -mergedMGridCheck false
    setVerifyGeometryMode -minHole false
    setVerifyGeometryMode -implantCheck false
    setVerifyGeometryMode -minimumCut false
    setVerifyGeometryMode -minStep false
    setVerifyGeometryMode -viaEnclosure false
    setVerifyGeometryMode -antenna false
    setVerifyGeometryMode -insuffMetalOverlap false
    setVerifyGeometryMode -pinInBlkg false
    setVerifyGeometryMode -diffCellViol false
    setVerifyGeometryMode -sameCellViol false
    setVerifyGeometryMode -padFillerCellsOverlap false
    setVerifyGeometryMode -routingBlkgPinOverlap false
    setVerifyGeometryMode -routingCellBlkgOverlap false
    setVerifyGeometryMode -regRoutingOnly false
    setVerifyGeometryMode -stackedViasOnRegNet false
    setVerifyGeometryMode -wireExt false
    setVerifyGeometryMode -useNonDefaultSpacing false
    setVerifyGeometryMode -maxWidth false
    setVerifyGeometryMode -maxNonPrefLength -1
    setVerifyGeometryMode -error 10000000
    catch { verifyGeometry -report $evalGeoRpt }

    catch { verifyConnectivity -noFloatingMetal -error 10000000 -report $evalConRpt }
    #catch { verifyConnectivity -type all -noAntenna -noUnConnPin -geomConnect -noSoftPGConnect -noFloatingMetal -error 10000000 -report $evalConRpt }

    exit
}

proc loadDesign { lef def } {
    catch { loadLefFile $lef }
    catch { loadDefFile $def }
}
