#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "peer.h"
#include "message.h"
#include "bitfield.h"

extern Bitmap *bitmap;

//指向当前与之通信的peer列表
Peer *peer_head = NULL;

//初始化Peer结构体
int initialize_peer(Peer *peer)
{
	if (peer == NULL)
		return -1;
	peer->socket = -1;
	memset(peer->ip, 0, 16);
	peer->port = 0;
	memset(peer->id, 0, 21);
	peer->state = INITIAL;

	peer->in_buff = NULL;
	peer->out_msg = NULL;
	peer->out_msg_copy = NULL;

	peer->in_buff = (char *)malloc(MSG_SIZE);
	if (peer->in_buff == NULL)
		goto OUT;
	memset(peer->in_buff, 0, MSG_SIZE);
	peer->buff_len = 0;

	peer->out_msg = (char *)malloc(MSG_SIZE);
	if (peer->out_msg == NULL)
		goto OUT;
	memset(peer->out_msg, 0, MSG_SIZE);
	peer->msg_len = 0;

	peer->out_msg_copy = (char *)malloc(MSG_SIZE);
	if (peer->out_msg_copy == NULL)
		goto OUT;
	memset(peer->out_msg_copy, 0, MSG_SIZE);
	peer->msg_copy_len = 0;
	peer->msg_copy_index = 0;

	peer->am_choking = 1;
	peer->am_interested = 0;
	peer->peer_choking = 1;
	peer->peer_interested = 0;

	peer->bitmap.bitfield = NULL;
	peer->bitmap.bitfield_length = 0;
	peer->bitmap.valid_length = 0;

	peer->Request_piece_head = NULL;
	peer->Requested_piece_head = NULL;

	peer->down_total = 0;
	peer->up_total = 0;

	peer->start_timestamp = 0;
	peer->recet_timestamp = 0;

	peer->last_down_timestamp = 0;
	peer->last_up_timestamp = 0;
	peer->down_count = 0;
	peer->up_count = 0;
	peer->down_rate = 0.0;
	peer->up_rate = 0.0;

	peer->next = (Peer *)0;
	return 0;

OUT:
	if (peer->in_buff != NULL)
		free(peer->in_buff);
	if (peer->out_msg != NULL)
		free(peer->out_msg);
	if (peer->out_msg_copy != NULL)
		free(peer->out_msg_copy);

	return -1;
}

//向peer链表添加一个结点
Peer *add_peer_node()
{
	int ret;
	Peer *node, *p;

	//分配内存空间
	node = (Peer *)malloc(sizeof(Peer));
	if (node == NULL) {
		printf("%s:%d error\n", __FILE__, __LINE__);
		return NULL;
	}
	//进行初始化
	ret = initialize_peer(node);
	if (ret < 0) {
		printf("%s:%d error\n", __FILE__, __LINE__);
		free(node);
		return NULL;
	}
	//将node加入到peer链表中
	if (peer_head == NULL) {
		peer_head = node;	//node为peer链表的第一个结点
	} else {
		p = peer_head;		//使p指针指向peer链表的最后一个结点
		while(p->next != NULL)
			p = p->next;
		p->next = node;
	}

	return node;
}

//从peer链表中删除一个结点
int del_peer_node(Peer *peer)
{
	Peer *p = peer_head, *q;
	if (peer == NULL)
		return -1;
		
	while (p != NULL) {
		if (p == peer) {
			if (p == peer_head)
				peer_head = p->next;
			else
				q->next = p->next;
			free_peer_node(p);
			return 0;
		} else {
			q = p;
			p = p->next;
		}
	}	

	return -1;
}

//撤销当前请求队列
int cancel_request_list(Peer *node)
{
	Request_piece *p = node->Request_piece_head;

	while(p != NULL) {
		node->Request_piece_head = node->Request_piece_head->next;
		free(p);
		p = node->Request_piece_head;
	}

	return 0;
}

//撤销当前被请求队列
int cancel_requested_list(Peer *node)
{
	Request_piece *p = node->Requested_piece_head;

	while(p != NULL) {
		node->Requested_piece_head = node->Requested_piece_head->next;
		free(p);
		p = node->Requested_piece_head;
	}

	return 0;
}

//释放一个peer结点的内存
void free_peer_node(Peer *node)
{
	if (node == NULL)
		return;
	if (node->bitmap.bitfield != NULL)
		free(node->bitmap.bitfield);
	if (node->in_buff != NULL)
		free(node->in_buff);
	if (node->out_msg != NULL)
		free(node->out_msg);
	if (node->out_msg_copy != NULL)
		free(node->out_msg_copy);

	cancel_request_list(node);
	cancel_requested_list(node);

	free(node);
}

//释放peer管理模块中动态深情的内存
void release_memory_in_peer()
{
	Peer *p;

	if (peer_head == NULL)
		return;
	p = peer_head;
	while(p != NULL) {
		peer_head = peer_head->next;
		free_peer_node(p);
		p = peer_head;
	}
}

//打印peer结点的一些信息，用于调试程序
void print_peers_data()
{
	Peer *p = peer_head;
	int index = 0;
	while(p != NULL) {
		printf("peer: %d IP %s down_rate: %.2f \n", index, p->ip, p->down_rate);
		index++;
		p = p->next;
	}
}
