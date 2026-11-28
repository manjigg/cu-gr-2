#pragma once
#include "IncrementalRoute.h"

namespace native3d {
// 在 Stage 3 入口冻结同一资源账本，重复测量候选生成；本实验不会提交任何路径。
Json measureMazeSearch(const IncumbentSolution&,const std::vector<std::pair<double,int>>&,int stride);
void saveMazeCheckpoint(const IncumbentSolution&,const std::vector<std::pair<double,int>>&,const std::string& path);
std::vector<std::pair<double,int>> loadMazeCheckpoint(IncumbentSolution&,const std::string& path);
void saveStage2Checkpoint(const IncumbentSolution&,const std::string& path);
void loadStage2Checkpoint(IncumbentSolution&,const std::string& path);
}
