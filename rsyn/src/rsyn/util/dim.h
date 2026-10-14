// 文件功能：定义 X/Y 维度和按维度索引的二维数据基础类型。
// 【零基础阅读】Dimension 用 X/Y 命名两个轴，二维结构可按 [X]/[Y] 下标访问。
// 类比两栏表：一个位置有横坐标和纵坐标，访问轴编号不等于访问某个格子。
// 公共二维模板被 DBU/float/double 坐标复用，字段数相同但数值类型可不同。
/* Copyright 2014-2018 Rsyn
 * dimension/d 为轴选择 X/Y，二维成员 x/y 为该类型的两项；索引访问不改变坐标参照系。
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#ifndef RSYN_DIM_H
#define RSYN_DIM_H

#include <string>

// -----------------------------------------------------------------------------

// [NOTE] Although this class use indexing for accessing data members, most of
//        time the indexing can be resolved in compiling time so that there's
//        no overhead. When it's not possible to do so, by using indexing an
//        "if" is likely to be avoided which is good for code optimization since
//        it prevents the pipeline to be flushed.

// -----------------------------------------------------------------------------

enum Dimension {
	X = 0,
	Y = 1
}; // end enum

inline std::string getDimension(const Dimension dim ) {
	switch(dim) {
		case X  : return "X";
		case Y  : return "Y";
		default : return "?";
	} // end switch
} // end method

const Dimension REVERSE_DIMENSION[2] = {Y,X};

#define for_each_dimension(variable) for ( int variable = 0; variable < 2; variable++ )

// -----------------------------------------------------------------------------

enum Boundary {
	LOWER = 0,
	UPPER = 1,

	INNER = 2,
	OUTER = 3
}; // end enum

#define for_each_boundary(variable) for ( int variable = 0; variable < 2; variable++ )

#endif /* DIM_H */
