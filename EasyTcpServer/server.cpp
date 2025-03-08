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
			printf("�˳�cmdThread�߳�\n");
			break;
		}
		else
		{
			printf("��֧�ֵ�����\n");
		}
	}
}

class MyServer:public EasyTcpServer {
public: 
	//ֻ�ᱻһ���̵߳��� ��ȫ
	virtual void OnNetJoin(ClientSocket* pClient) {
		EasyTcpServer::OnNetJoin(pClient);
		//printf("client<%d> join\n", pClient->getSockfd());
	}

	//cellserver 4 ����̴߳�������ȫ
	virtual void OnNetLeave(ClientSocket* pClient) {
		EasyTcpServer::OnNetLeave(pClient);
		//printf("client<%d> leave\n", pClient->getSockfd());
	}

	//cellserver 4 ����̴߳�������ȫ
	virtual void OnNetMsg(CellServer* pCellServer,ClientSocket* pClient, DataHeader* header) {
		EasyTcpServer::OnNetMsg(pCellServer,pClient,header);
		switch (header->cmd)
		{
		case CMD_LOGIN:
		{
			Login* login = (Login*)header;
			//printf("�յ��ͻ���<Socket=%d>����:CMD_LOGIN,���ݳ��ȣ�%d,username: %s,password: %s\n", (int)cSocket, login->dataLength, login->userName, login->passWord);
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
			//printf("�յ��ͻ���<Socket=%d>����:CMD_LOGINOUT,���ݳ��ȣ�%d,username: %s\n", (int)cSocket, loginout->dataLength, loginout->userName);
			LoginoutResult oret;
			//SendData(cSocket, &oret);
		}
		break;
		default:
		{
			printf("�յ�<cSocket=%d>δ������Ϣ�����ݳ��ȣ�%d\n", (int)pClient->getSockfd(), header->dataLength);
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

	//����UI�߳�
	std::thread t1(cmdThread);
	t1.detach();

	while (g_bRun) {
		server.onRun();
	}
	server.Close();
	printf("������˳�\n");
	getchar();
	return 0;
}