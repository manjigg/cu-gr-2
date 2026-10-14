// 文件功能：输出 FLUTE 辅助算法错误消息；区分仅报告和报告后退出两种处理方式。
// 【零基础阅读】错误消息写到输出后，是否继续由调用的函数版本决定。
// exit 结束整个进程，不只是退出当前一层循环；return 则只是返回调用者。
// 本模块是 C 辅助错误设施，不做 C++ 异常 throw/catch 处理。
#include <stdio.h>
// msg 为输入错误消息；printf/fprintf 输出文字，exit 给进程失败状态，不是路由评分。
#include <stdlib.h>

/**************************************************************************/
/*
  print error message and continue
*/

void  err_msg(
char* msg
)
{
  fprintf(stderr, "%s\n", msg);
}

/**************************************************************************/
/*
  print error message and  exit
*/

void  err_exit(
char* msg
)
{
  fprintf(stderr, "%s\n", msg);
  exit(1);
}
