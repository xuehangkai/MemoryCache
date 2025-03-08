#include"EasyTcpClient.hpp"
#include"CELLTimestamp.hpp"
#include<thread>
#include<atomic>



bool g_bRun = true;
void cmdThread() {
	while (g_bRun) {
		char cmdBuf[256] = {};
#ifdef _WIN32
		scanf_s("%s", cmdBuf, 256);
#else
		scanf("%s", cmdBuf);
#endif
		if (0 == strcmp(cmdBuf, "exit")) {
			g_bRun = false;
			printf("退出cmdThread线程\n");
			break;
		}
		else
		{
			printf("不支持的命令\n");
		}
	}
}

//客户端数量
const int cCount = 1000;
//发送线程数量
const int tCount = 4;
//客户端数组
EasyTcpClient* client[cCount];

std::atomic_int sendCount = 0;
std::atomic_int readyCount = 0;

void sendThread(int id) {
	printf("thread<%d>, start!\n", id);
	int c= cCount / tCount;
	int begin = (id-1)*c;
	int end = id* c;
	for (int n = begin; n < end; n++) {
		if (!g_bRun) {
			return;
		}
		client[n] = new EasyTcpClient();
	}
	for (int n = begin; n < end; n++) {
		if (!g_bRun) {
			return;
		}
		client[n]->Connect("192.168.31.247", 4567);
		//client[n]->Connect("127.0.0.1", 4567);
		//printf("thread<%d>, Connect=%d\n",id, n);
	}

	//EasyTcpClient client;
	//client.initSocket();
	//client.Connect("127.0.0.1",4567);
	printf("thread<%d>, Connect<begin=%d, end=%d>\n", id, begin,end);

	readyCount++;
	while (readyCount<tCount) {
		//等待其他线程准备好发送数据
		std::chrono::milliseconds t(10);
		std::this_thread::sleep_for(t);
	}

	Login login[10];
	for (int n = 0; n < 10;n++) {
#ifdef _WIN32
		strcpy_s(login[n].userName, "xhk");
		strcpy_s(login[n].passWord, "xhk123");
#else
		strcpy(login[n].userName, "xhk");
		strcpy(login[n].passWord, "xhk123");
#endif
	}
	const int nLen = sizeof(login);
	while (g_bRun) {
		//client.OnSelect();
		//client.SendData(&login);
		for (int n = begin; n < end; n++) {
			if (SOCKET_ERROR !=client[n]->SendData(login, nLen)) {
				sendCount++; 
			}
			client[n]->OnSelect();
		}
	}
	//client.Close();
	for (int n = 0; n < cCount; n++) {
		client[n]->Close();
		delete client[n];
	}
	printf("thread<%d>, exit!\n", id);
}
int main() {

	//启动UI线程
	std::thread t1(cmdThread);
	t1.detach();

	//启动发送线程
	for (int n = 0; n < tCount; n++) {
		std::thread t1(sendThread,n+1);
		t1.detach();
	}
	CELLTimestamp tTime;
	while (g_bRun) {
		auto t = tTime.getElapsedSecond();
		if (t>=1.0) {
			printf("thread<%d>, client<%d>, time<%lf>, send<%d>\n",tCount, cCount,t,(int)(sendCount/t));
			sendCount = 0;
			tTime.update();
		}
		Sleep(1);
	}
	printf("已退出。\n");
	return 0;
}