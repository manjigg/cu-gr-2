// 文件功能：定义颜色数据及颜色转换/访问助手，供可视化工具使用。
// 【零基础阅读】颜色对象把各颜色通道/表示放在一起，供界面或图形输出使用。
// 颜色是展示方式，不是路径层号、容量或拥塞分数本身。
// 构造/转换方法控制数字怎样显示成颜色，与路线计算解耦。
/* Copyright 2014-2018 Rsyn
 * 颜色通道是显示数值，透明度描述绘图混合；颜色编号不是金属层编号或路由器真实评分。
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
 
/* 
 * File:   Color.h
 * Author: jucemar
 *
 * Created on March 19, 2015, 1:52 PM
 */

#ifndef COLOR_H
#define	COLOR_H

class Color {
public:
	unsigned char r, g, b;

	bool operator == (const Color& c) const { return r == c.r && g == c.g && b == c.b; };
	
	bool transparent;
	
	Color() {
		r = 255;
		g = 255;
		b = 255;
		transparent = false;
	}

	Color(const unsigned char red, const unsigned char green, const unsigned char blue) {
		setRGB(red, green, blue);
	}

	void setRGB(const unsigned char red, const unsigned char green, const unsigned char blue) {
		r = red;
		g = green;
		b = blue;
	} // end method
	
	void setRGB(Color color) {
		r = color.r;
		g = color.g;
		b = color.b;
	} // end method
}; // end class


#endif	/* COLOR_H */
