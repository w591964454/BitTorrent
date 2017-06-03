#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>

#include "parse_metafile.h"
#include "bitfield.h"
#include "message.h"
#include "sha1.h"
#include "data.h"
#include "policy.h"
#include "torrent.h"

extern char *file_name;		//待下载文件的文件名
extern Files *files_head;	//对于多文件种子有效，存放各个文件上的路径和长度
extern int file_length;		//待下载文件的总长度
extern int piece_length;	//每个piece的长度
extern char *pieces;		//存放所有piece的hash值
extern int pieces_length;	//缓冲区pieces的长度

extern Bitmap *bitmap;			//指向己方的位图
extern int download_piece_num;	//记录已经下载了多少个piece
extern Peer *peer_head;			//指向peer链表

#define btcache_len 1024		//缓冲区中共有多少个Btcache结点
Btcache *btcache_head = NULL;	//指向一个大小为16MB的缓冲区
Btcache *last_piece = NULL;		//存放待下载文件的最后一个piece
int last_piece_index = 0;		//最后一个piece的索引，它的值为总piece数减1
int last_piece_count = 0;		//针对最后一个piece，记录下载了多少个slice
int last_slice_len = 0;			//最后一个piece的最后一个slice长度

int *fds = NULL;			//存放文件描述符
int fds_len = 0;			//指针fds所指向的数组的长度
int have_piece_index[64];	//存放刚刚下载到的piece的索引
int end_mode = 0;			//是否进入了终端模式

//创建Btcache结点，分配内存空间并对其成员进行初始化
Btcache *initialize_btcache_node()
{
	Btcache *node;

	node = (Btcache *)malloc(sizeof(Btcache));
	if (node == NULL) {
		return NULL;
	}
	node->buff = (unsigned char *)malloc(16 * 1024);
	if (node->buff == NULL) {
		if (node != NULL)
			free(node);
			return NULL;
	}

	node->index = -1;
	node->begin = -1;
	node->length = -1;

	node->in_use = 0;
	node->read_write = -1;
	node->is_full = 0;
	node->is_writed = 0;
	node->access_count = 0;
	node->next = NULL;

	return node;
}

//创建总大小为16K * 1024bit即16MB的缓冲区
int create_btcache()
{
	int i;
	Btcache *node, *last;	//node 指向刚刚创建的结点，last指向缓冲区中最后一个结点

	for (i = 0; i < btcache_len; i++) {
		node = initialize_btcache_node();
		if (node == NULL) {
			printf("%s:%d creat_btcache error\n", __FILE__, __LINE__);
			release_memory_in_btcache();
			return -1;
		}
		if (btcache_head == NULL) {
			btcache_head = node;
			last = node;
		} else {
			last->next = node;
			last = node;
		}
	}
	//为存储最后一个piece申请空间
	int count = file_length % piece_length / (16 * 1024);
	if (file_length % piece_length % (16 * 1024) != 0)
		count++;
	last_piece_count = count; //count为最后一个piece所含的slice数
	last_slice_len = file_length % piece_length % (16 * 1024);
	if (last_slice_len == 0)
		last_slice_len = 16 * 1024;
	last_piece_index = pieces_length / 20 - 1; //最后一个piece的index值
	while (count > 0) {
		node = initialize_btcache_node();
		if (node == NULL) {
			printf("%s:%d creat_btcache error\n", __FILE__, __LINE__);
			release_memory_in_btcache();
			return -1;
		}
		if (last_piece == NULL) {
			last_piece = node;
			last = node;
		} else {
			last->next = node;
			last = node;
		}

		count--;
	}

	for (i = 0; i < 64; i++) {
		have_piece_index[i] = -1;
	}

	return 0;
}

//释放data.c文件中动态分配的内存
void release_memory_in_btcache()
{
	Btcache *p = btcache_head;
	while (p != NULL) {
		btcache_head = p->next;
		if (p->buff != NULL)
			free(p->buff);
		free(p);
		p = btcache_head;
	}

	release_last_piece();
	if (fds != NULL)
		free(fds);
}

//释放为存储最后一个piece而申请的空间
void release_last_piece()
{
	Btcache *p = last_piece;
	while (p != NULL) {
		last_piece = p->next;
		if (p->buff != NULL)
			free(p->buff);
		free(p);
		p = last_piece;
	}
}

//判断种子文件中待下载的文件个数
int get_files_count()
{
	int count = 0;

	if (is_multi_files() == 0)
		return 1;
	Files *p = files_head;
	while (p != NULL) {
		count ++;
		p = p->next;
	}

	return count;
}

//根据种子文件中的信息创建保存下载数据的文件
int create_files()
{
	int ret, i;
	char buff[1] = {0x0};

	fds_len = get_files_count();
	if (fds_len < 0)
		return -1;
	fds = (int *)malloc(fds_len * sizeof(int));
	if (fds == NULL)
		return -1;

	if (is_multi_files() == 0) { //待下载的为单文件
		*fds = open(file_name, O_RDWR | O_CREAT, 0777);
		if (*fds < 0) {
			printf("%s:%d error", __FILE__, __LINE__);
			return -1;
		}
		ret = lseek(*fds, file_length - 1, SEEK_SET);
		if (ret < 0) {
			printf("%s:%d error", __FILE__, __LINE__);
			return -1;
		}
		ret = write(*fds, buff, 1);
		if (ret != 1) {
			printf("%s:%d error", __FILE__, __LINE__);
			return -1;
		}
	} else { //待下载的是多个文件
		ret = chdir(file_name);
		if (ret < 0) { //改变目录失败，说明该目录还未创建
			ret = mkdir(file_name, 0777);
			if (ret < 0) {
				printf("%s:%d error", __FILE__, __LINE__);
				return -1;
			}
			ret = chdir(file_name);
			if (ret < 0) {
				printf("%s:%d error", __FILE__, __LINE__);
				return -1;
			}
		}
		Files *p = files_head;
		i = 0;
		while(p != NULL) {
			fds[i] = open(p->path, O_RDWR | O_CREAT, 0777);
			if (fds[i] < 0) {
				printf("%s:%d error", __FILE__, __LINE__);
				return -1;
			}
			ret = lseek(fds[i], p->length - 1, SEEK_SET);
			if (ret < 0) {
				printf("%s:%d error", __FILE__, __LINE__);
				return -1;
			}
			ret = write(fds[i], buff, 1);
			if (ret != 1) {
				printf("%s:%d error", __FILE__, __LINE__);
				return -1;
			}

			p = p->next;
			i++;
		} //while循环结束
	} //end else

	return 0;
}

//判断一个Btcache结点的数据要写到哪个文件以及具体位置，并写入
int write_btcache_node_to_harddisk(Btcache *node)
{
	long long line_position;
	Files *p;
	int i;

	if ((node == NULL) || (fds == NULL))
		return -1;

	//无论是否下载多文件，将要下载的数据看成一个线性字节流，line_position指示要写入硬盘的线性位置，piece_length为每个piece长度，它被定义在parse_metafile.c中
	line_position = node->index * piece_length + node->begin;

	if (is_multi_files() == 0) { //如果下载的是单文件
		lseek(*fds, line_position, SEEK_SET);
		write(*fds, node->buff, node->length);
		return 0;
	}
	//下载的是多文件
	if (files_head == NULL) {
		printf("%s:%d file_head is NULL", __FILE__, __LINE__);
		return -1;
	}
	p = files_head;
	i = 0;
	while (p != NULL) {
		if ((line_position < p->length) && (line_position + node->length < p->length)) {
			//待写入的数据属于同一个文件
			lseek(fds[i], line_position, SEEK_SET);
			write(fds[i], node->buff, node->length);
			break;
		} else if ((line_position < p->length) && (line_position + node->length >= p->length)) {
			//待写入的数据跨越了两个文件或两个以上的文件
			int offset = 0; //buff内偏移，也是已写的字节数
			int left = node->length; //剩余要写的字节数

			lseek(fds[i], line_position, SEEK_SET);
			write(fds[i], node->buff, p->length - line_position);
			offset = p->length - line_position; //offset存放已写的字节数
			left = left - (p->length - line_position); //还需写的字节数
			p = p->next; //用于获取下一个文件
			i++;

			while (left > 0) {
				if (p->length >= left) { //当前文件的长度大于等于要写入的字节数
					lseek(fds[i], 0, SEEK_SET);
					write(fds[i], node->buff + offset, left); //写入剩余要写的字节数
					left = 0;
				} else { //当前文件的长度小于要写的字节数
					lseek(fds[i], 0, SEEK_SET);
					write(fds[i], node->buff + offset, p->length); //写满当前文件
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}
			}
			break;
		} else {
			//待写入的数据不应写入当前文件
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	}
	
	return 0;
}

//从硬盘读取一个slice的数据，存放到缓冲区中，在peer需要时发送
int read_slice_from_harddisk(Btcache *node)
{
	unsigned int line_position;
	Files *p;
	int i;

	if ((node == NULL) || (fds == NULL))
		return -1;
	if ((node->index >= piece_length / 20) || (node->begin >= piece_length) || (node->length > 16 * 1024))
		return -1;
	//计算线性偏移量
	line_position = node->index * piece_length + node->begin;
	if (is_multi_files() == 0) { //如果下载的是单个文件
		lseek(*fds, line_position, SEEK_SET);
		read(*fds, node->buff, node->length);
		return 0;
	}

	//如果下载的是多个文件
	if (files_head == NULL)
		get_files_length_path();
	p = files_head;
	i = 0;
	while (p != NULL) {
		if ((line_position < p->length) && (line_position + node->length < p->length)) {
			//待读出的数据属于同一个文件
			lseek(fds[i], line_position, SEEK_SET);
			read(fds[i], node->buff, node->length);
			break;
		} else if ((line_position < p->length) && (line_position + node->length >= p->length)) {
			//待读出的数据跨越了两个文件或两个以上的文件
			int offset = 0; //buff内的偏移，也是已读的字节数
			int left = node->length; //剩余要读的字节数

			lseek(fds[i], line_position, SEEK_SET);
			read(fds[i], node->buff, p->length - line_position);
			offset = p->length - line_position; //offsset存放已读的字节数
			left = left - (p->length - line_position); //还需再读的字节数
			p = p->next; //用于获取下一个文件的长度
			i++; //获取下一个文件描述符
			while (left > 0) {
				if (p->length >= left) { //当前文件的长度大于等于要读的字节数
					lseek(fds[i], 0, SEEK_SET);
					read(fds[i], node->buff + offset, left); //读取剩余要读的字节数
					left = 0;
				} else { //当前文件的长度小于要读的字节数
					lseek(fds[i], 0, SEEK_SET);
					read(fds[i], node->buff + offset, p->length); //读取当前文件的所有内容
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}

				break;
			}
		} else {
			//待读出的数据不应写入当前文件
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	}
	return 0;
}

//从peer队列中删除对某个piece的请求
int delete_request_end_mode(int index)
{
	Peer *p = peer_head;
	Request_piece *req_p, *req_q;

	if (index < 0 || index >= piece_length / 20)
		return -1;
	while (p != NULL) {
		req_p = p->Request_piece_head;
		while (req_p != NULL) {
			if (req_p->index == index) {
				if (req_p == p->Request_piece_head)
					p->Request_piece_head = req_p->next;
				else
					req_q->next = req_p->next;

				req_p = p->Request_piece_head;
				continue;
			}
			req_q = req_p;
			req_p = req_p->next;
		}
		p = p->next;
	}
	return 0;
}

//检查一个piece的数据是否正确，若正确存入硬盘
int write_piece_to_harddisk(int sequence, Peer *peer)
{
	Btcache *node_ptr = btcache_head, *p;
	unsigned char piece_hash1[20], piece_hash2[20];
	int slice_count = piece_length / (16 * 1024); //一个piece所含的slice数
	int index, index_copy;

	if (peer == NULL)
		return -1;
	int i = 0;
	while (i < sequence) {
		node_ptr = node_ptr->next;
		i++;
	}
	p = node_ptr; //p指针指向piece的第一个slice所在的btcache结点

	//计算刚刚下载到的这个piece的hash值
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	while (slice_count > 0 && node_ptr != NULL) {
		SHA1Update(&ctx, node_ptr->buff, 16 * 1024);
		slice_count--;
		node_ptr = node_ptr->next;
	}
	SHA1Final(piece_hash1, &ctx);
	//从种子文件中获取该piece的正确的hash值
	index = p->index * 20;
	index_copy = p->index; //存放piece的index
	for (i = 0; i < 20; i++)
		piece_hash2[i] = pieces[index + i];
	//比较两个hash值，若两者一致说明下载了一个正确的piece
	int ret = memcmp(piece_hash1, piece_hash2, 20);
	if (ret != 0) {
		printf("piece hash is wrong\n");
		return -1;
	}
	//将该piece的所有slice写入文件
	node_ptr = p;
	slice_count = piece_length / (16 * 1024);
	while (slice_count > 0) {
		write_btcache_node_to_harddisk(node_ptr);
		//在peer的请求队列中删除piece请求
		Request_piece *req_p = peer->Request_piece_head;
		Request_piece *req_q = peer->Request_piece_head;
		while (req_p != NULL) {
			if (req_p->begin == node_ptr->begin && req_p->index == node_ptr->index) {
				if (req_p == peer->Request_piece_head)
					peer->Request_piece_head = req_p->next;
				else
					req_q->next = req_p->next;
				free(req_p);
				req_p = req_q = NULL;
				break;
			}
			req_q = req_p;
			req_p = req_p->next;
		}

		node_ptr->index = -1;
		node_ptr->begin = -1;
		node_ptr->length = -1;
		node_ptr->in_use = 0;
		node_ptr->read_write = -1;
		node_ptr->is_full = 0;
		node_ptr->is_writed = 0;
		node_ptr->access_count = 0;
		node_ptr = node_ptr->next;
		slice_count--;
	}
	//当前处于终端模式，则在peer链表中删除所有对该piece的请求
	if (end_mode == 1)
		delete_request_end_mode(index_copy);
	//更新位图
	set_bit_value(bitmap, index_copy, 1);
	//保存piece的index，准备给所有的peer发送have消息
	for (i = 0; i < 64; i++) {
		if (have_piece_index[i] == -1) {
			have_piece_index[i] = index_copy;
			break;
		}
	}
	//更新download_piece_num,每下载10个piece就将位图写入文件
	download_piece_num++;
	if (download_piece_num % 10 == 0)
		restore_bitmap();
	//打印出提示信息
	printf("%%%%%% Total piece download: %d %%%%%%", download_piece_num);
	printf("writed piece index: %d \n", index_copy);
	return 0;
}

//从硬盘中的文件读取一个piece到p指针所指向的缓冲区中
int read_piece_from_harddisk(Btcache *p, int index)
{
	Btcache *node_ptr = p;
	int begin = 0;
	int length = 16 * 1024;
	int slice_count = piece_length / (16 * 1024);
	int ret;

	if (p == NULL || index >= piece_length / 20)
		return -1;
	while (slice_count > 0) {
		node_ptr->index = index;
		node_ptr->begin = begin;
		node_ptr->length = length;

		ret = read_slice_from_harddisk(node_ptr);
		if (ret < 0)
			return -1;

		node_ptr->in_use = 1;
		node_ptr->read_write = 0;
		node_ptr->is_full = 1;
		node_ptr->is_writed = 0;
		node_ptr->access_count = 0;

		begin += 16 * 1024;
		slice_count--;
		node_ptr = node_ptr->next;
	}

	return 0;
}

//将整个缓冲区中已下载的piece写入硬盘
int write_btcache_to_harddisk(Peer *peer)
{
	Btcache *p = btcache_head;
	int slice_count = piece_length / (16 * 1024);
	int index_count = 0;
	int full_count = 0;
	int first_index;

	while (p != NULL) {
		if (index_count % slice_count == 0) {
			full_count = 0;
			first_index = index_count;
		}
		if ((p->in_use == 1) && (p->read_write == 1) && (p->is_full == 1) && (p->is_writed == 0)) {
			full_count++;
		}
		if (full_count == slice_count) {
			write_piece_to_harddisk(first_index, peer);
		}
		index_count++;
		p = p->next;
	}

	return 0;
}

//当缓冲区不够用时，释放从硬盘上读取的piece
int release_read_btcache_node(int base_count)
{
	Btcache *p = btcache_head;
	Btcache *q = NULL;
	int count = 0;
	int used_count = 0;
	int slice_count = piece_length / (16 * 1024);

	if (base_count < 0)
		return -1;

	while (p != NULL) {
		if (count % slice_count == 0) {
			used_count = 0;
			q = p;
		}
		if (p->in_use == 1 && p->read_write == 0)
			used_count += p->access_count;
		if (used_count == base_count)
			break; //找到一个空闲的piece

		count++;
		p = p->next;
	}

	if (p != NULL) {
		p = q;
		while (slice_count > 0) {
			p->index = -1;
			p->begin = -1;
			p->length = -1;
			p->in_use = 0;
			p->read_write = -1;
			p->is_full = 0;
			p->is_writed = 0;
			p->access_count = 0;

			slice_count--;
			p = p->next;
		}
	}

	return 0;
}

//下载完一个slice后，检查是否该slice为piece的最后一个slice
int is_a_complete_piece(int index, int *sequence)
{
	Btcache *p = btcache_head;
	int slice_count = piece_length / (16 * 1024);
	int count = 0;
	int num = 0;
	int complete = 0;

	while (p != NULL) {
		if (count % slice_count == 0 && p->index != index) {
			num = slice_count;
			while (num > 0 && p != NULL) {
				p = p->next;
				num--;
				count++;
			}
			continue;
		}
		if (count % slice_count != 0 || p->read_write != 1 || p->is_full != 1)
			break;

		*sequence = count;
		num = slice_count;

		while (num > 0 && p != NULL) {
			if (p->index == index && p->read_write == 1 && p->is_full == 1)
				complete++;
			else
				break;

			num--;
			p = p->next;
		}
		break;
	}

	if (complete == slice_count)
		return 1;
	else
		return 0;
}

//将整个缓冲区中所存的所有数据清空
void clear_btcache()
{
	Btcache *node = btcache_head;
	while (node != NULL) {
		node->index = -1;
		node->begin = -1;
		node->length = -1;
		node->in_use = 0;
		node->read_write = -1;
		node->is_full = 0;
		node->is_writed = 0;
		node->access_count = 0;
		node = node->next;
	}
}

//将从peer处获取的一个slice存储到缓冲区中
int write_slice_to_btcache(int index, int begin, int length, unsigned char *buff, int len, Peer *peer)
{
	int count = 0, slice_count, unuse_count;
	Btcache *p = btcache_head, *q = NULL; //q指针指向每个piece第一个slice

	if (p == NULL)
		return -1;
	if (index >= piece_length / 20 || begin > piece_length - 16 * 1024)
		return -1;
	if (buff == NULL || peer == NULL)
		return -1;
	if (index == last_piece_index) {
		write_slice_to_last_piece(index, begin, length, buff, len, peer);
		return 0;
	}

	//处于终端模式时，先判断该slice所在的piece是否已被下载
	if (end_mode == 1) {
		if (get_bit_value(bitmap, index) == 1)
			return 0;
	}

	//遍历缓冲区，检查当前slice所在的piece的其他数据是否已存在
	//若存在说明不是一个新的piece，若不存在说明是一个新的piece
	slice_count = piece_length / (16 * 1024);
	while (p != NULL) {
		if (count % slice_count == 0)
			q = p;
		if (p->index == index && p->in_use == 1)
			break;

		count++;
		p = p->next;
	}

	//p非空说明当前slice所在的piece的部分
	if (p != NULL) {
		count = begin / (16 * 1024); //count存放当前要存的slice在piece中的索引值
		p = q;
		while (count > 0) {
			p  = p->next;
			count--;
		}

		if (p->begin == begin && p->in_use == 1 && p->read_write == 1 && p->is_full == 1)
			return 0; //该piece已经存在

		p->index = index;
		p->begin = begin;
		p->length = length;

		p->in_use = 1;
		p->read_write = 1;
		p->is_full = 1;
		p->is_writed = 0;
		p->access_count = 0;

		memcpy(p->buff, buff, len);
		printf("+++++ write a slice to btcache index:%-6d begin:%-6x +++++\n", index, begin);

		//如果是刚刚开始下载（下载到的piece不足10个），则立即写入硬盘并告知peer
		if (download_piece_num < 10) {
			int sequence;
			int ret;
			ret = is_a_complete_piece(index, &sequence);
			if (ret == 1) {
				printf("###### begin write a piece to harddisk ######\n");
				write_piece_to_harddisk(sequence, peer);
				printf("###### end write a piece to harddisk ######\n");
			}
		}
		return 0;
	}

	//p为空说明当前slice是其所在的piece第一块下载到的数据
	//首先判断是否存在空的缓冲区，若不存在，则将已下载的写入硬盘
	int i = 4;
	while (i > 0) {
		slice_count = piece_length / (16 * 1024);
		count = 0; //计数当前指向第几个slice
		unuse_count = 0; //计数当前piece中有多少个空的slice
		Btcache *q;
		p = btcache_head;
		while (p != NULL) {
			if (count % slice_count == 0) {
				unuse_count = 0;
				q = p;
			}
			if (p->in_use == 0)
				unuse_count++;
			if (unuse_count == slice_count)
				break; //找到一个空闲的piece

			count++;
			p = p->next;
		}

		if (p != NULL) {
			p = q;
			count = begin / (16 * 1024);
			while (count > 0) {
				p = p->next;
				count--;
			}

			p->index = index;
			p->begin = begin;
			p->length = length;

			p->in_use = 1;
			p->read_write = 1;
			p->is_full = 1;
			p->is_writed = 0;
			p->access_count = 0;

			memcpy(p->buff, buff, len);
			printf("+++++ write a slice to btcache index:%-6d begin:%-6x +++++\n", index, begin);
			return 0;
		}

		if (i == 4)
			write_btcache_to_harddisk(peer);
		if (i == 3)
			release_read_btcache_node(16);
		if (i == 2)
			release_read_btcache_node(8);
		if (i == 1)
			release_read_btcache_node(0);
		i--;
	}

	//如果还没有空闲的缓冲区，丢弃下载到的这个slice
	printf("+++++ write a slice to btcache FAILED : NO BUFFER +++++\n");
	clear_btcache();
	return 0;
}

//从缓冲区中获取一个slice，读取的slice存放到buff指向的数组中
//若缓冲区中不存在该slice，则从硬盘读slice所在的piece到缓冲区中
int read_slice_for_send(int index, int begin, int length, Peer *peer)
{
	Btcache *p = btcache_head, *q; //q指向每个slice第一个slice
	int ret;

	//检查参数是否有误
	if (index >= piece_length / 20 || begin > piece_length - 16 * 1024)
		return -1;
	ret = get_bit_value(bitmap, index);
	if (ret < 0) {
		printf("Peer requested slice did not download\n");
		return -1;
	}
	if (index == last_piece_index) {
		read_slice_for_send_last_piece(index, begin, length, peer);
		return 0;
	}
	//待获取得slice缓冲区中已存在
	while (p != NULL) {
		if (p->index == index && p->begin == begin && p->length == length && p->in_use == 1 && p->is_full) {
			//构造piece消息
			ret = create_piece_msg(index, begin, p->buff, p->length, peer);
			if (ret < 0) {
				printf("Function create piece msg error\n");
				return -1;
			}
			p->access_count = 1;
			return 0;
		}
		p = p->next;
	}

	int i = 4, count, slice_count, unuse_count;
	while (i > 0) {
		slice_count = piece_length / (16 * 1024);
		count = 0; //计数当前指向第几个slice
		p = btcache_head;

		while (p != NULL) {
			if (count % slice_count == 0) {
				unuse_count = 0;
				q = p;
			}
			if (p->in_use == 0)
				unuse_count++;
			if (unuse_count == slice_count)
				break; //找到一个空闲的piece

			count++;
			p = p->next;
		}

		if ( p != NULL) {
			read_piece_from_harddisk(q, index);

			p = q;
			while (p != NULL) {
				if (p->index == index && p->begin == begin && p->length == length && p->in_use == 1 && p->is_full == 1) {
					//构造piece消息
					ret = create_piece_msg(index, begin, p->buff, p->length, peer);
					if (ret < 0) {
						printf("Function create piece msg error\n");
						return -1;
					}
					p->access_count = 1;
					return 0;
				}
				p = p->next;
			}
		}

		if (i == 4)
			write_btcache_to_harddisk(peer);
		if (i == 3)
			release_read_btcache_node(16);
		if (i == 2)
			release_read_btcache_node(8);
		if (i == 1)
			release_read_btcache_node(0);
		i--;
	}

	//如果实在没有缓冲区了，就不读slice所在的piece到缓冲区中
	p = initialize_btcache_node();
	if (p == NULL) {
		printf("%s:%d allocate memory error\n", __FILE__, __LINE__);
		return -1;
	}
	p->index = index;
	p->begin = begin;
	p->length = length;
	read_slice_from_harddisk(p);
	//构造piece消息
	ret = create_piece_msg(index, begin, p->buff, p->length, peer);
	if (ret < 0) {
		printf("Function create piece msg error\n");
		return -1;
	}
	//释放刚刚申请的内存
	if (p->buff != NULL)
		free(p->buff);
	if (p != NULL)
		free(p);

	return 0;
}

void clear_btcache_before_peer_close(Peer *peer)
{
	Request_piece *req = peer->Request_piece_head;
	int i = 0, index[2] = {-1, -1};

	if (req == NULL)
		return;
	while (req != NULL && i < 2) {
		if (req->index != index[i]) {
			index[i] = req->index;
			i++;
		}
		req = req->next;
	}
	Btcache *p = btcache_head;
	while (p != NULL) {
		if (p->index != -1 && (p->index == index[0] || p->index == index[1])) {
			p->index = -1;
			p->begin = -1;
			p->length = -1;

			p->in_use = 0;
			p->read_write = -1;
			p->is_full = 0;
			p->is_writed = 0;
			p->access_count = 0;
		}
		p = p->next;
	}
}

int write_last_piece_to_btcache(Peer *peer)
{
	int index = last_piece_index, i;
	unsigned char piece_hash1[20], piece_hash2[20];
	Btcache *p = last_piece;

	//校验piece的HASH值
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	while (p != NULL) {
		SHA1Update(&ctx, p->buff, p->length);
		p = p->next;
	}
	SHA1Final(piece_hash1, &ctx);

	for (i = 0; i < 20; i++)
		piece_hash2[i] = pieces[index * 20 + i];

	if (memcmp(piece_hash1, piece_hash2, 20) == 0) {
		printf("@@@@@@ last piece download OK @@@@@@\n");
	} else {
		printf("@@@@@@ last piece download NOT OK @@@@@@\n");
		return -1;
	}

	p = last_piece;
	while (p != NULL) {
		write_btcache_node_to_harddisk(p);
		p = p->next;
	}
	printf("@@@@@@ last piece write to harddisk OK @@@@@@\n");

	//在peer中的请求队列中删除piece请求
	//更新位图
	set_bit_value(bitmap, index, 1);

	//准备发送have消息
	for (i = 0; i < 64; i++) {
		if (have_piece_index[i] == -1) {
			have_piece_index[i] = index;
			break;
		}
	}

	download_piece_num++;
	if (download_piece_num % 10 == 0)
		restore_bitmap();

	return 0;
}

int write_slice_to_last_piece(int index, int begin, int length, unsigned char *buff, int len, Peer *peer)
{
	if (index != last_piece_index || begin > (last_piece_count - 1) * 16 * 1024)
		return -1;

	if (buff == NULL || peer == NULL)
		return -1;

	//定位到要写入哪个slice
	int count = begin / (16 * 1024);
	Btcache *p = last_piece;
	while (p != NULL || count > 0) {
		count--;
		p = p->next;
	}

	if (p->begin == begin && p->in_use == 1 && p->is_full == 1)
		return 0; //该slice已存在

	p->index = index;
	p->begin = begin;
	p->length = length;

	p->in_use = 1;
	p->read_write = 1;
	p->is_full = 1;
	p->is_writed = 0;
	p->access_count = 0;

	memcpy(p->buff, buff, len);

	p = last_piece;
	while (p != NULL) {
		if (p->is_full != 1)
			break;
		p = p->next;
	}
	if (p == NULL) {
		write_last_piece_to_btcache(peer);
	}

	return 0;
}

int read_last_piece_from_harddisk(Btcache *p, int index)
{
	Btcache *node_ptr = p;
	int begin = 0;
	int length = 16 * 1024;
	int slice_count = last_piece_count;
	int ret;

	if (p == NULL || index != last_piece_index)
		return -1;
	while (slice_count > 0) {
		node_ptr->index = index;
		node_ptr->begin = begin;
		node_ptr->length = length;
		if (begin == (last_piece_count - 1) * 16 * 1024)
			node_ptr->length = last_slice_len;

		ret = read_slice_from_harddisk(node_ptr);
		if (ret < 0)
			return -1;

		node_ptr->in_use = 1;
		node_ptr->read_write = 0;
		node_ptr->is_full = 1;
		node_ptr->is_writed = 0;
		node_ptr->access_count = 0;

		begin += 16 * 1024;
		slice_count--;
		node_ptr = node_ptr->next;
	}

	return 0;
}

int read_slice_for_send_last_piece(int index, int begin, int length, Peer *peer)
{
	Btcache *p;
	int ret, count = begin / (16 * 1024);

	//检查参数是否有误
	if (index != last_piece_index || begin > (last_piece_count - 1) * 16 * 1024)
		return -1;

	ret = get_bit_value(bitmap, index);
	if (ret < 0) {
		printf("peer requested slice did not download\n");
		return -1;
	}

	p = last_piece;
	while (count > 0) {
		p = p->next;
		count--;
	}
	if (p->is_full != 1) {
		ret = read_last_piece_from_harddisk(last_piece, index);
		if (ret < 0)
			return -1;
	}

	if (p->in_use == 1 && p->is_full == 1) {
		ret = create_piece_msg(index, begin, p->buff, p->length, peer);
	}

	if (ret == 0)
		return 0;
	else
		return -1;
}
