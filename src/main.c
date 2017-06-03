#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include "parse_metafile.h"
#include "signal_hander.h"
#include "bitfield.h"
#include "data.h"
#include "policy.h"
#include "tracker.h"
#include "torrent.h"
#include "log.h"

//如果取消以下这行的注释，将打印许多用于调试程序的消息
//#define DEBUG

int main(int argc, char *argv[])
{
	int ret;

	if (argc != 2) {
		printf("usage:%s metafile\n", argv[0]);
		exit(-1);
	}

	ret = set_signal_hander();				//设置信号处理函数
	if (ret != 0) {
		printf("%s:%d error\n", __FILE__, __LINE__);
		return -1;
	}

	ret = parse_metafile(argv[1]);			//解析种子文件
	if (ret != 0) {
		printf("%s:%d error\n", __FILE__, __LINE__);
		return -1;
	}

	ret = create_files();					//创建用于保存下载数据的文件
	if (ret != 0) {
		printf("%s:%d error\n", __FILE__, __LINE__);
		return -1;
	}

	ret = create_bitfield();				//创建位图
	if (ret != 0) {
		printf("%s:%d error\n", __FILE__, __LINE__);
		return -1;
	}

	ret = create_btcache();					//创建缓冲区
	if (ret != 0) {
		printf("%s:%d error\n", __FILE__, __LINE__);
		return -1;
	}

	init_unchoke_peers();					//初始化非阻塞 peer

	download_upload_with_peers();			//负责与所有 Peer 收发数据、交换消息

	do_clear_work();						//下载完成后做一些清理工作，主要是释放动态分配的内存

	return 0;
}
