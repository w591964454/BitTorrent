#ifndef POLICY_H
#define POLICY_H
#include "peer.h"

#define COMPUTE_RATE_TIME 10			//每隔10秒计算一次各个peer的下载和上传速度
#define UNCHOKE_COUNT 4					//非阻塞peer的个数
#define REQ_SLICE_NUM 4					//每次请求slice的个数

typedef struct _Unchoke_peers {
	Peer*	unchkpeer[UNCHOKE_COUNT];	//保存非阻塞peer的指针
	int		count;						//记录当前有多少个非阻塞peer
	Peer*	optunchkpeer;				//保存优化非阻塞peer的指针
} Unchoke_peers;

void init_unchoke_peers();				//初始化plicy.c中定义的全局变量unchoke_peers
int	select_unchoke_peer();				//选择unchoke peer
int select_optunchoke_peer();			//从peer队列中选择一个优化非阻塞peer
int compute_rate();						//计算最近一段时间（10s）每个peer的上传下载速度
int compute_total_rate();				//计算总的上传下载速度

int is_seed(Peer *node);				//判断某个peer是否为种子
int create_req_slice_msg(Peer *node);	//构造数据请求

#endif
