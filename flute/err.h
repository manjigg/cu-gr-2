// 文件功能：声明错误报告 err_msg 与终止程序 err_exit 接口。
// 【零基础阅读】err_msg 只报告，err_exit 报告后退出，两种错误处理强度不同。
// char* msg 是文字地址，void 返回类型表示函数不返回普通结果值。
// 声明在这里，真正打印/退出在 err.c，调用处决定何时使用。
#ifndef _ERR_H_
#define _ERR_H_
// msg 为错误文字地址；void 无普通返回值；err_exit 终止进程而不是仅返回调用函数。

void  err_msg(
char* msg
);

void  err_exit(
char* msg
);

#endif
