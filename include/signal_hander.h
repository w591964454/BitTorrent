#ifndef SIGNAL_HEADER_H
#define SIGNAL_HEADER_H

//做一些清理工作，如释放动态分配的内存
void do_clear_work();

//处理一些新号
void process_signal(int signo);

//设置信号处理函数
int set_signal_hander();

#endif
