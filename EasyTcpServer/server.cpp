//#include "Alloctor.h"
#include "EasyTcpServer.hpp"
#include<thread>

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

class MyServer:public EasyTcpServer {
public: 
	//只会被一个线程调用 安全
	virtual void OnNetJoin(ClientSocket* pClient) {
		EasyTcpServer::OnNetJoin(pClient);
		//printf("client<%d> join\n", pClient->getSockfd());
	}

	//cellserver 4 多个线程触发不安全
	virtual void OnNetLeave(ClientSocket* pClient) {
		EasyTcpServer::OnNetLeave(pClient);
		//printf("client<%d> leave\n", pClient->getSockfd());
	}

	//cellserver 4 多个线程触发不安全
	virtual void OnNetMsg(CellServer* pCellServer,ClientSocket* pClient, DataHeader* header) {
		EasyTcpServer::OnNetMsg(pCellServer,pClient,header);
		switch (header->cmd)
		{
		case CMD_LOGIN:
		{
			Login* login = (Login*)header;
			//printf("收到客户端<Socket=%d>请求:CMD_LOGIN,数据长度：%d,username: %s,password: %s\n", (int)cSocket, login->dataLength, login->userName, login->passWord);
			//LoginResult ret ;
			//SendData(cSocket, &ret);
			//pClient->SendData(&ret);
			LoginResult* ret=new LoginResult();

			pCellServer->addSendTask(pClient,ret);

		}
		break;
		case CMD_LOGINOUT:
		{
			Loginout* loginout = (Loginout*)header;
			//printf("收到客户端<Socket=%d>请求:CMD_LOGINOUT,数据长度：%d,username: %s\n", (int)cSocket, loginout->dataLength, loginout->userName);
			LoginoutResult oret;
			//SendData(cSocket, &oret);
		}
		break;
		default:
		{
			printf("收到<cSocket=%d>未定义消息，数据长度：%d\n", (int)pClient->getSockfd(), header->dataLength);
			DataHeader header;
			//SendData(cSocket,&header);
		}
		break;
		}
	}

	virtual void OnNetRecv(ClientSocket* pClient) {
		EasyTcpServer::OnNetRecv(pClient);
	}

private:
};


int main() {

	MyServer server;
	server.iniSocket();
	server.Bind(nullptr, 4567);
	server.Listen(5);
	server.Start(4);

	//启动UI线程
	std::thread t1(cmdThread);
	t1.detach();

	while (g_bRun) {
		server.onRun();
	}
	server.Close();
	printf("服务端退出\n");
	getchar();
	return 0;
}