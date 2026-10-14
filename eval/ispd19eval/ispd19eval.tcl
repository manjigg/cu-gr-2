# 文件功能：在 Innovus 中加载输入设计及可选路由 DEF，执行 ISPD 2019 DRC 和连通性检查。
# evaluate 输出两个报告路径；loadDesign 分别加载 LEF、原始 DEF 和增量路由结果。
# 【零基础阅读】先读完整设计，再可选 defIn 叠加路由；随后设 DRC 模式并生成报告。
# verify_drc 检几何规则，verifyConnectivity 检连接，检查设置与报告是两个步骤。
# proc 是函数定义，catch 捕获命令失败，exit 会结束工具进程而非只返回当前过程。
# 实际检查需 Innovus 提供命令，文本中的 8 是工具本地 CPU 设置而非金属层数量。
setMultiCpuUsage -localCpu 8
proc evaluate { inputLef inputDef outputDef evalGeoRpt evalConRpt } {
    # inputDef 基础设计，outputDef 可选增量路由；两个报告路径分别接收几何/連通检查。

    loadDesign $inputLef $inputDef $outputDef
    
    set_verify_drc_mode -disable_rules {}
    set_verify_drc_mode -check_implant true
    set_verify_drc_mode -check_implant_across_rows false
    set_verify_drc_mode -check_ndr_spacing false
    set_verify_drc_mode -check_only default
    set_verify_drc_mode -check_same_via_cell false
    set_verify_drc_mode -exclude_pg_net false
    set_verify_drc_mode -ignore_trial_route false
    set_verify_drc_mode -report $evalGeoRpt
    set_verify_drc_mode -limit 10000000
    catch { verify_drc }

#   catch { verifyConnectivity -noFloatingMetal -error 10000000 -report $evalConRpt }
    catch { verifyConnectivity -type all -noAntenna -noUnConnPin -geomConnect -noSoftPGConnect -noFloatingMetal -error 10000000 -report $evalConRpt }

    exit
}

proc loadDesign { lef idef odef } {
    catch { loadLefFile $lef }
    catch { loadDefFile $idef }
    if { $odef != "" } {
        catch { defIn $odef }
    }
}
