#ifndef TORRENT_H
#define TORRENT_H
#include "tracker.h"

int download_upload_with_peers();			//负责与所有 peer 收发数据、交换信息

int print_peer_list();						//打印 peer 链表中各个 peer 的 IP 和端口
void print_process_info();					//打印下载进度消息
void clear_connect_tracker();				//释放与连接 Tracker 有关的一些动态存储空间
void clear_connect_peer();					//释放与连接 peer 有关的一些动态存储空间
void clear_tracker_response();				//释放与解析 Tracker 回应有关的一些动态存储空间
void release_memory_in_torrent();			//释放 torrent.c 中动态申请的存储空间

#endif
