#ifndef MWSSAGE_H
#define MESSAGE_H
#include "peer.h"

int int_to_char(int i, unsigned char c[4]);	//将整形变量i的4个字节放到数组c中
int char_to_int(unsigned char c[4]);		//将数组c中的4个字节转换为一个整形数

//以下函数创建各个类型的消息，创建消息的函数请参考BT协议加以理解
int create_handshake_msg(char *info_hash, char *peer_id, Peer *peer);
int create_keep_alive_msg(Peer *peer);
int create_chock_interested_msg(int type, Peer *peer);
int create_have_msg(int index, Peer *peer);
int create_bitfield_msg(char *bitfield, int bitfield_len, Peer *peer);
int create_request_msg(int index, int begin, int length, Peer *peer);
int create_piece_msg(int index, int begin, char *block, int b_len, Peer *peer);
int create_cancel_msg(int index, int begin, int length, Peer *peer);
int create_port(int port, Peer *peer);

//判断接收缓冲区中是否存放了一个完整的消息
int is_complete_message(unsigned char *buff, unsigned int len, int *ok_len);
//处理受到的消息，接收缓冲区中存放的一条完整的信息
int parse_response(Peer *peer);
//处理受到的消息， 接收缓冲区中除了存放着一条完整消息外，还有其他不完整消息
int parse_response_uncomplete_msg(Peer *p, int ok_len);
//根据当前的状态创建相应消息
int create_response_message(Peer *peer);
//为发送have消息做准备， have消息比较特殊，它要发送给所有peer
int prepare_send_have_msg();
//即将与peer断开时， 丢弃套接字发送缓冲区中的所有未发送消息
void discard_send_buffer(Peer *peer);

#endif
