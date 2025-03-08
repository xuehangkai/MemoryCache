#ifndef _EasyTcpServer_hpp_
#define _EasyTcpServer_hpp_

#ifdef _WIN32
	#define FD_SETSIZE	2506
	#define WIN32_LEAN_AND_MEAN
	#define _WINSOCK_DEPRECATED_NO_WARNINGS 
	#include<Windows.h>
	#include<WinSock2.h>
	#pragma comment(lib,"ws2_32.lib")
#else
	#include<unistd.h>// uni std
	#include<arpa/inet.h>
	#include<string.h>

	#define SOCKET int
	#define INVALID_SOCKET (SOCKET)(~0)
	#define SOCKET_ERROR		(-1)
#endif

#include<stdio.h>
#include <vector>
#include <map>
#include<thread>
#include<mutex>
#include "MessageHeader.hpp"	
#include"CELLTimestamp.hpp"
#include <functional>
#include<atomic>
#include"CELLTask.hpp"
#include"CELLObjectPool.hpp"

#ifndef RECV_BUFF_SZIE
	//��������С��Ԫ��С
	#define RECV_BUFF_SZIE 10240*5
	#define SEND_BUFF_SZIE RECV_BUFF_SZIE
#endif

//�ͻ�����������
class ClientSocket :public ObjectPoolBase<ClientSocket,1000>
{
public:
	ClientSocket(SOCKET sockfd=INVALID_SOCKET) {
		_sockfd = sockfd;
		memset(_szMsgBuf,0, RECV_BUFF_SZIE);
		_lastPos = 0;

		memset(_szSendBuf, 0, SEND_BUFF_SZIE);
		_lastSendPos = 0;
	}
	SOCKET getSockfd() {
		return _sockfd;
	}
	char* getMsgBuf() {
		return _szMsgBuf;
	}
	int getLastPos() {
		return _lastPos;
	}
	void setLastPos(int pos) {
		_lastPos = pos;
	}
	//��������
	int SendData(DataHeader* header) {
		int ret = SOCKET_ERROR;
		//Ҫ���͵����ݳ���
		int nSendLen = header->dataLength;
		//Ҫ���͵�����
		const char* pSendData = (const char*)header;
		while (true) {
			if (_lastSendPos + nSendLen >= SEND_BUFF_SZIE)
			{
				//������Կ��������ݳ���
				int nCopyLen = SEND_BUFF_SZIE - _lastSendPos;
				//��������
				memcpy(_szSendBuf + _lastSendPos, pSendData, nCopyLen);
				//����ʣ������λ��
				pSendData += nCopyLen;
				//����ʣ�����ݳ���
				nSendLen -= nCopyLen;
				//��������
				ret = send(_sockfd, _szSendBuf, SEND_BUFF_SZIE, 0);
				//����β��������
				_lastSendPos = 0;
				if (ret == SOCKET_ERROR) {
					return ret;
				}
			}
			else {
				//��Ҫ���͵����ݿ��������ͻ�����β��
				memcpy(_szSendBuf + _lastSendPos, pSendData, nSendLen);
				//��������β��λ��
				_lastSendPos += nSendLen;
				break;
			}
		}
		return ret;
	}
private :
	// socket fd_set file desc set
	SOCKET _sockfd;
	//�ڶ������� ��Ϣ������
	char _szMsgBuf[RECV_BUFF_SZIE];
	//��Ϣ������������β��λ��
	int _lastPos;

	//�ڶ������� ���ͻ�����
	char _szSendBuf[SEND_BUFF_SZIE];
	//��Ϣ������������β��λ��
	int _lastSendPos;
};

class CellServer;

//�����¼��ӿ�
class INetEvent {
public:
	//���麯��
	//�ͻ��˼����¼�
	virtual void OnNetJoin(ClientSocket* pClient) = 0;
	//�ͻ��뿪�¼�
	virtual void OnNetLeave(ClientSocket* pClient) = 0;
	//�ͻ�����Ϣ�¼�
	virtual void OnNetMsg(CellServer* pCellServer,ClientSocket* pClient, DataHeader* header) = 0;
	//recv�¼�
	virtual void OnNetRecv(ClientSocket* pClient) = 0;

private:

};

//������Ϣ��������
class CellSendMsg2ClientTask :public CellTask {
	ClientSocket* _pClient;
	DataHeader* _pHeader;

public:
	CellSendMsg2ClientTask(ClientSocket* pClient, DataHeader* header) {
		_pClient = pClient;
		_pHeader = header;
	}

	//ִ������
	void doTask() {
		_pClient->SendData(_pHeader);
		delete _pHeader;
	}

};

//������Ϣ���մ��������
class CellServer
{
public:
	CellServer(SOCKET sock=INVALID_SOCKET) {
		_sock = sock;
		_pNetEvent = nullptr;
	}
	~CellServer() {
		Close();
		_sock = INVALID_SOCKET;
	}

	void setEventObj(INetEvent* event) {
		_pNetEvent = event;
	}

	//�ر�socket
	void Close() {
		//�ر�Win Socket 2.x����
		if (_sock != INVALID_SOCKET) {
#ifdef	_WIN32
			for (int n = (int)_clients.size() - 1; n >= 0; n--)
			{
				closesocket(_clients[n]->getSockfd());
				delete _clients[n];
			}
			closesocket(_sock);
#else
			for (int n = (int)_clients.size() - 1; n >= 0; n--)
			{
				close(_clients[n]->getSockfd());
				delete _clients[n];
			}
			close(_sock);
#endif
			_clients.clear();
		}
	}
	//�Ƿ���
	bool isRun() {
		return _sock != INVALID_SOCKET;
	}
	//����������Ϣ
	//���ݿͻ�socket fd_set
	fd_set _fdRead_bak;
	//�ͻ��б��Ƿ��б仯
	bool _clients_change ;
	SOCKET _maxSock;
	void onRun() {
		_clients_change = true;
		while (isRun()) 
		{
			if (_clientsBuff.size()>0) 
			{
				//�ӻ��������ȡ���ͻ�����
				std::lock_guard<std::mutex> lock(_mutex);
				for (auto pClient : _clientsBuff) {
					_clients[pClient->getSockfd()] = pClient;
				}
				_clientsBuff.clear();
				_clients_change = true;
			}
			if (_clients.empty()) {
				std::chrono::milliseconds t(1);
				std::this_thread::sleep_for(t);
				continue;
			}
			fd_set fdRead;
			FD_ZERO(&fdRead);
			if (_clients_change) {
				_clients_change = false;
				_maxSock = _clients.begin()->second->getSockfd();
				for (auto iter : _clients)
				{
					FD_SET(iter.second->getSockfd(), &fdRead);
					if (_maxSock < iter.second->getSockfd())
					{
						_maxSock = iter.second->getSockfd();
					}
				}
				memcpy( &_fdRead_bak, &fdRead, sizeof(fd_set));
			}
			else {
				memcpy(  &fdRead, &_fdRead_bak, sizeof(fd_set));
			}
			///nfds ��һ������ֵ ��ָfd_set����������������(socket)�ķ�Χ������������
			///���������ļ����������ֵ+1 ��Windows�������������д0
			int ret = select(_maxSock + 1, &fdRead, nullptr, nullptr, nullptr);
			if (ret < 0) {
				printf("select���������\n");
				Close();
				return;
			}
			else if (ret == 0) {
				continue;
			}
#ifdef _WIN32
			for (int n = 0; n <fdRead.fd_count; n++)
			{
				auto iter = _clients.find(fdRead.fd_array[n]);
				if (iter!=_clients.end()) {
					if (-1 == RecvData(iter->second)) {		
						if (_pNetEvent) {
							_pNetEvent->OnNetLeave(iter->second);
						}
						_clients_change = true;
						delete iter->second;
						_clients.erase(iter->first);
					}
				}
				else {
					printf("error. if (iter != _clients.end())\n");
				}
			}
#else
			std::vector<ClientSocket*> temp;
			for (auto iter : _clients)
			{
				if (FD_ISSET(iter.second->getSockfd(), &fdRead))
				{
					if (-1 == RecvData(iter.second))
					{
						if (_pNetEvent)
							_pNetEvent->OnNetLeave(iter.second);
						_clients_change = true;
						temp.push_back(iter.second);
					}
				}
			}
			for (auto pClient : temp)
			{
				_clients.erase(pClient->getSockfd());
				delete pClient;
			}
#endif
			//printf("����ʱ�䴦������ҵ�񡣡�\n");
		}
	}
	//�������� ����ճ�� ��ְ�
	int RecvData(ClientSocket* pClient) {
		//���տͻ�������
		char* szRecv = pClient->getMsgBuf() + pClient->getLastPos(); 

		int nLen = (int)recv(pClient->getSockfd(), szRecv,(RECV_BUFF_SZIE)- pClient->getLastPos(), 0);
		_pNetEvent->OnNetRecv(pClient);
		if (nLen <= 0) {
			//printf("�ͻ���<Socket=%d>�˳�,�������\n", (int)pClient->getSockfd());
			return -1;
		}
		//����ȡ�������ݿ�������Ϣ������
		//memcpy(pClient->getMsgBuf() + pClient->getLastPos(), _szRecv, nLen);
		//��Ϣ������������β��λ�ú���
		pClient->setLastPos(pClient->getLastPos() + nLen);
		//�ж���Ϣ�������������Ƿ񳤶ȴ�����ϢͷDataHeader�ĳ���
		while (pClient->getLastPos() >= sizeof(DataHeader)) {
			//��ʱ�Ϳ���֪����ǰ��Ϣ�ĳ���
			DataHeader* header = (DataHeader*)pClient->getMsgBuf();
			//�ж���Ϣ�����������ݳ��ȴ�����Ϣ����
			if (pClient->getLastPos() >= header->dataLength) {
				//��Ϣ������ʣ��δ�������ݳ���
				int nSize = pClient->getLastPos() - header->dataLength;
				//����������Ϣ
				OnNetMsg(pClient, header);
				//����Ϣ������ʣ��δ��������ǰ��
				memcpy(pClient->getMsgBuf(), pClient->getMsgBuf() + header->dataLength, nSize);
				//��Ϣ������������β��λ��ǰ��
				pClient->setLastPos(nSize);
			}
			else {
				//��Ϣ������ʣ�����ݲ���һ��������Ϣ
				break;
			}
		}
		return 0;
	}
	//��Ӧ������Ϣ
	virtual void OnNetMsg(ClientSocket* pClient, DataHeader* header) {
		_pNetEvent->OnNetMsg(this,pClient, header);
		//_recvCount++;
		//auto t1 = _tTime.getElapsedSecond();
		//if (t1 >= 1.0) {
		//	printf("time<%lf>, socket<%d>, csocket<%d>, clients<%d>, recvCount<%d>\n", t1, _sock, cSocket, _clients.size(), _recvCount);
		//	_recvCount = 0;
		//	_tTime.update();
		//}
	}

	void addclient(ClientSocket* pClient) {
		std::lock_guard<std::mutex> lock(_mutex);
		//_mutex.lock();
		_clientsBuff.push_back(pClient);
		//_mutex.unlock();
	}

	void Start() {
		_thread =std::thread(std::mem_fn(&CellServer::onRun), this);
		_taskServer.Start();
	}

	size_t getClientCount() {
		return _clients.size()+ _clientsBuff.size();
	}
	void addSendTask(ClientSocket* pClient, DataHeader* header) {
		CellSendMsg2ClientTask* task = new CellSendMsg2ClientTask(pClient, header);
		_taskServer.addTask(task);
	}
private:
	SOCKET _sock;
	//��ʽ�ͻ�����
	std::map<SOCKET,ClientSocket*> _clients;
	//����ͻ�����
	std::vector<ClientSocket*> _clientsBuff;
	//������е���
	std::mutex _mutex;
	std::thread _thread;
	//�����¼�����
	INetEvent* _pNetEvent;
	//
	CellTaskServer _taskServer;
};

//new �ڶ��ڴ�
class EasyTcpServer : public INetEvent
{
private:
	SOCKET _sock;
	//��Ϣ��������ڲ��ᴴ���߳�
	std::vector<CellServer*> _cellServers;
	//ÿ����Ϣ��ʱ
	CELLTimestamp _tTime;
protected:
	//�յ���Ϣ����
	std::atomic_int _recvCount;
	//�ͻ��˼���
	std::atomic_int _clientCount;
	//recv��������
	std::atomic_int _msgCount;

public:
	EasyTcpServer() {
		_sock = INVALID_SOCKET;
		_recvCount = 0;
		_clientCount = 0;
		_msgCount = 0;
	}
	virtual ~EasyTcpServer() {
		Close();
	}

	//��ʼ��socket
	SOCKET iniSocket() {
#ifdef _WIN32
		//����Win Socket 2.x����
		WORD ver = MAKEWORD(2, 2);
		WSADATA dat;
		WSAStartup(ver, &dat);
#endif
		//1������һ��socket
		if (INVALID_SOCKET != _sock) {
			printf("sock=%d�رվ�����\n",(int) _sock);
			Close();
		}
		_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == _sock) {
			printf("�����׽���ʧ��\n");
		}
		else
		{
			printf("����sock=%d�׽��ֳɹ�\n",(int)_sock);
		}
		return _sock;
	}
	//��ip�Ͷ˿ں�
	int Bind(const char* ip,unsigned short port) {
		if (INVALID_SOCKET == _sock) {
			iniSocket();
		}
		sockaddr_in _sin = {};
		_sin.sin_family = AF_INET;
		_sin.sin_port = htons(port);//host to net unsigned short
#ifdef	_WIN32
		if (ip) {
			_sin.sin_addr.S_un.S_addr = inet_addr(ip);
		}
		else {
			_sin.sin_addr.S_un.S_addr = INADDR_ANY;
		}
#else
		if (ip) {
			_sin.sin_addr.s_addr = inet_addr(ip);
		}
		else {
			_sin.sin_addr.s_addr = INADDR_ANY;
		}
#endif
		int ret = bind(_sock, (sockaddr*)&_sin, sizeof(_sin));
		if (ret == SOCKET_ERROR)
		{
			printf("�󶨶˿�<%d>ʧ��\n",port);
		}
		else {
			printf("�󶨶˿�<%d>�ɹ�\n",port);
		}
		return ret;
	}
	//�����˿ں�
	int Listen(int n) {
		int ret = listen(_sock, n);
		if (SOCKET_ERROR ==ret) {
			printf("socket=<%d>�����˿�ʧ��\n",(int)_sock);
		}
		else {
			printf("socket=<%d>�����˿ڳɹ�\n",(int)_sock);
		}
		return ret;
	}
	//���ܿͻ�������
	SOCKET Accept() {
		sockaddr_in clientAddr = {};
		int nAddrLen = sizeof(sockaddr_in);
		SOCKET cSocket = INVALID_SOCKET;
#ifdef _WIN32
		cSocket = accept(_sock, (sockaddr*)&clientAddr, &nAddrLen);
#else
		cSocket = accept(_sock, (sockaddr*)&clientAddr, (socklen_t*)&nAddrLen);
#endif
		if (INVALID_SOCKET == cSocket) {
			printf("socket=<%d>���ܵ���Ч�Ŀͻ���socket\n",(int)_sock);
		}
		else
		{
			NewUserJoin userJoin;
			//SendDataToAll(&userJoin);
			addClientToCellServer(new ClientSocket(cSocket));
			//printf("socket=<%d>�¿ͻ��˼��룺csocket=%d ,ip= %s \n", (int)_sock,(int)cSocket, inet_ntoa(clientAddr.sin_addr));
		}
		return cSocket;
	}
	//���¿ͻ��˷�����ͻ�������С��cellserver
	void addClientToCellServer(ClientSocket* pClient) {
		//���ҿͻ���������cellserver��Ϣ�������
		auto pMinServer = _cellServers[0];
		for (auto pCellServer: _cellServers) {
			if (pMinServer->getClientCount() > pCellServer->getClientCount()) {
				pMinServer = pCellServer;
			}
		}
		pMinServer->addclient(pClient);
		OnNetJoin(pClient);
	}

	void Start(int nCellServer) {
		for (int n = 0; n < nCellServer;n++) {
			auto ser = new CellServer(_sock);
			_cellServers.push_back(ser);
			//ע�������¼����ܶ���
			ser->setEventObj(this);
			//���������߳�
			ser->Start();
		}
	}

	//�ر�socket
	void Close() {
		//�ر�Win Socket 2.x����
		if (_sock != INVALID_SOCKET) {
#ifdef	_WIN32
			closesocket(_sock);
			WSACleanup();
#else
			close(_sock);
#endif
		}
	}
	//����������Ϣ
	bool onRun() {
		if (isRun()) {
			time4msg();
			fd_set fdRead;
			//fd_set fdWrite;
			//fd_set fdExp;
			FD_ZERO(&fdRead);
			//FD_ZERO(&fdWrite);
			//FD_ZERO(&fdExp);
			FD_SET(_sock, &fdRead);
			//FD_SET(_sock, &fdWrite);
			//FD_SET(_sock, &fdExp);

			timeval t = { 0,10 };
			int ret = select(_sock + 1, &fdRead,nullptr, nullptr, &t);
			if (ret < 0) {
				printf("select���������\n");
				Close();
				return false;
			}
			//�ж���������socket���Ƿ��ڼ�����
			if (FD_ISSET(_sock,&fdRead)) {
				FD_CLR(_sock, &fdRead);
				Accept();
				return true;
			}
			//printf("����ʱ�䴦������ҵ�񡣡�\n");
			return true;
		}
		return false;
	}
	//�Ƿ���
	bool isRun() {
		return _sock != INVALID_SOCKET;
	}

	//���㲢���ÿ���յ���������Ϣ
	void time4msg() {
		auto t1 = _tTime.getElapsedSecond();
		if (t1 >=1.0) {
			printf("thread<%d>, time<%lf>, socket<%d>, clients<%d>, recvCount<%d>, msgCount<%d>\n",(int)_cellServers.size(), t1, (int)_sock, (int)_clientCount, (int)(_recvCount/t1), (int)(_msgCount / t1));
			_recvCount = 0;
			_msgCount = 0;
			_tTime.update();
		}
	}

	//ֻ�ᱻһ���̵߳��� ��ȫ
	virtual void OnNetJoin(ClientSocket* pClient) {
		_clientCount++;
	}

	//cellserver 4 ����̴߳�������ȫ
	virtual void OnNetLeave(ClientSocket* pClient) {
		_clientCount--;
	}

	//cellserver 4 ����̴߳�������ȫ
	virtual void OnNetMsg(CellServer* pCellServer,ClientSocket* pClient, DataHeader* header) {
		_msgCount++;
	}

	//
	virtual void OnNetRecv(ClientSocket* pClient) {
		_recvCount++;
	}
};

#endif