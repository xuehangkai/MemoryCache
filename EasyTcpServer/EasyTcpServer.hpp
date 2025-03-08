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
	//缓冲区最小单元大小
	#define RECV_BUFF_SZIE 10240*5
	#define SEND_BUFF_SZIE RECV_BUFF_SZIE
#endif

//客户端数据类型
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
	//发送数据
	int SendData(DataHeader* header) {
		int ret = SOCKET_ERROR;
		//要发送的数据长度
		int nSendLen = header->dataLength;
		//要发送的数据
		const char* pSendData = (const char*)header;
		while (true) {
			if (_lastSendPos + nSendLen >= SEND_BUFF_SZIE)
			{
				//计算可以拷贝的数据长度
				int nCopyLen = SEND_BUFF_SZIE - _lastSendPos;
				//拷贝数据
				memcpy(_szSendBuf + _lastSendPos, pSendData, nCopyLen);
				//计算剩余数据位置
				pSendData += nCopyLen;
				//计算剩余数据长度
				nSendLen -= nCopyLen;
				//发送数据
				ret = send(_sockfd, _szSendBuf, SEND_BUFF_SZIE, 0);
				//数据尾部置清零
				_lastSendPos = 0;
				if (ret == SOCKET_ERROR) {
					return ret;
				}
			}
			else {
				//将要发送的数据拷贝到发送缓冲区尾部
				memcpy(_szSendBuf + _lastSendPos, pSendData, nSendLen);
				//计算数据尾部位置
				_lastSendPos += nSendLen;
				break;
			}
		}
		return ret;
	}
private :
	// socket fd_set file desc set
	SOCKET _sockfd;
	//第二缓冲区 消息缓冲区
	char _szMsgBuf[RECV_BUFF_SZIE];
	//消息缓冲区的数据尾部位置
	int _lastPos;

	//第二缓冲区 发送缓冲区
	char _szSendBuf[SEND_BUFF_SZIE];
	//消息缓冲区的数据尾部位置
	int _lastSendPos;
};

class CellServer;

//网络事件接口
class INetEvent {
public:
	//纯虚函数
	//客户端加入事件
	virtual void OnNetJoin(ClientSocket* pClient) = 0;
	//客户离开事件
	virtual void OnNetLeave(ClientSocket* pClient) = 0;
	//客户端消息事件
	virtual void OnNetMsg(CellServer* pCellServer,ClientSocket* pClient, DataHeader* header) = 0;
	//recv事件
	virtual void OnNetRecv(ClientSocket* pClient) = 0;

private:

};

//网络消息发送任务
class CellSendMsg2ClientTask :public CellTask {
	ClientSocket* _pClient;
	DataHeader* _pHeader;

public:
	CellSendMsg2ClientTask(ClientSocket* pClient, DataHeader* header) {
		_pClient = pClient;
		_pHeader = header;
	}

	//执行任务
	void doTask() {
		_pClient->SendData(_pHeader);
		delete _pHeader;
	}

};

//网络消息接收处理服务类
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

	//关闭socket
	void Close() {
		//关闭Win Socket 2.x环境
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
	//是否工作
	bool isRun() {
		return _sock != INVALID_SOCKET;
	}
	//处理网络消息
	//备份客户socket fd_set
	fd_set _fdRead_bak;
	//客户列表是否有变化
	bool _clients_change ;
	SOCKET _maxSock;
	void onRun() {
		_clients_change = true;
		while (isRun()) 
		{
			if (_clientsBuff.size()>0) 
			{
				//从缓冲队列里取出客户数据
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
			///nfds 是一个整数值 是指fd_set集合中所有描述符(socket)的范围，而不是数量
			///既是所有文件描述符最大值+1 在Windows中这个参数可以写0
			int ret = select(_maxSock + 1, &fdRead, nullptr, nullptr, nullptr);
			if (ret < 0) {
				printf("select任务结束。\n");
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
			//printf("空闲时间处理其他业务。。\n");
		}
	}
	//接受数据 处理粘包 拆分包
	int RecvData(ClientSocket* pClient) {
		//接收客户端数据
		char* szRecv = pClient->getMsgBuf() + pClient->getLastPos(); 

		int nLen = (int)recv(pClient->getSockfd(), szRecv,(RECV_BUFF_SZIE)- pClient->getLastPos(), 0);
		_pNetEvent->OnNetRecv(pClient);
		if (nLen <= 0) {
			//printf("客户端<Socket=%d>退出,任务结束\n", (int)pClient->getSockfd());
			return -1;
		}
		//将收取到的数据拷贝到消息缓冲区
		//memcpy(pClient->getMsgBuf() + pClient->getLastPos(), _szRecv, nLen);
		//消息缓冲区的数据尾部位置后移
		pClient->setLastPos(pClient->getLastPos() + nLen);
		//判断消息缓冲区的数据是否长度大于消息头DataHeader的长度
		while (pClient->getLastPos() >= sizeof(DataHeader)) {
			//这时就可以知道当前消息的长度
			DataHeader* header = (DataHeader*)pClient->getMsgBuf();
			//判断消息缓冲区的数据长度大于消息长度
			if (pClient->getLastPos() >= header->dataLength) {
				//消息缓冲区剩余未处理数据长度
				int nSize = pClient->getLastPos() - header->dataLength;
				//处理网络消息
				OnNetMsg(pClient, header);
				//将消息缓冲区剩余未处理数据前移
				memcpy(pClient->getMsgBuf(), pClient->getMsgBuf() + header->dataLength, nSize);
				//消息缓冲区的数据尾部位置前移
				pClient->setLastPos(nSize);
			}
			else {
				//消息缓冲区剩余数据不够一条完整消息
				break;
			}
		}
		return 0;
	}
	//响应网络消息
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
	//正式客户队列
	std::map<SOCKET,ClientSocket*> _clients;
	//缓冲客户队列
	std::vector<ClientSocket*> _clientsBuff;
	//缓冲队列的锁
	std::mutex _mutex;
	std::thread _thread;
	//网络事件对象
	INetEvent* _pNetEvent;
	//
	CellTaskServer _taskServer;
};

//new 在堆内存
class EasyTcpServer : public INetEvent
{
private:
	SOCKET _sock;
	//消息处理对象，内部会创建线程
	std::vector<CellServer*> _cellServers;
	//每秒消息计时
	CELLTimestamp _tTime;
protected:
	//收到消息计数
	std::atomic_int _recvCount;
	//客户端计数
	std::atomic_int _clientCount;
	//recv函数计数
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

	//初始化socket
	SOCKET iniSocket() {
#ifdef _WIN32
		//启动Win Socket 2.x环境
		WORD ver = MAKEWORD(2, 2);
		WSADATA dat;
		WSAStartup(ver, &dat);
#endif
		//1、建立一个socket
		if (INVALID_SOCKET != _sock) {
			printf("sock=%d关闭旧连接\n",(int) _sock);
			Close();
		}
		_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == _sock) {
			printf("建立套接字失败\n");
		}
		else
		{
			printf("建立sock=%d套接字成功\n",(int)_sock);
		}
		return _sock;
	}
	//绑定ip和端口号
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
			printf("绑定端口<%d>失败\n",port);
		}
		else {
			printf("绑定端口<%d>成功\n",port);
		}
		return ret;
	}
	//监听端口号
	int Listen(int n) {
		int ret = listen(_sock, n);
		if (SOCKET_ERROR ==ret) {
			printf("socket=<%d>监听端口失败\n",(int)_sock);
		}
		else {
			printf("socket=<%d>监听端口成功\n",(int)_sock);
		}
		return ret;
	}
	//接受客户端连接
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
			printf("socket=<%d>接受到无效的客户端socket\n",(int)_sock);
		}
		else
		{
			NewUserJoin userJoin;
			//SendDataToAll(&userJoin);
			addClientToCellServer(new ClientSocket(cSocket));
			//printf("socket=<%d>新客户端加入：csocket=%d ,ip= %s \n", (int)_sock,(int)cSocket, inet_ntoa(clientAddr.sin_addr));
		}
		return cSocket;
	}
	//将新客户端分配给客户数量最小的cellserver
	void addClientToCellServer(ClientSocket* pClient) {
		//查找客户数量最少cellserver消息处理对象
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
			//注册网络事件接受对象
			ser->setEventObj(this);
			//启动服务线程
			ser->Start();
		}
	}

	//关闭socket
	void Close() {
		//关闭Win Socket 2.x环境
		if (_sock != INVALID_SOCKET) {
#ifdef	_WIN32
			closesocket(_sock);
			WSACleanup();
#else
			close(_sock);
#endif
		}
	}
	//处理网络消息
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
				printf("select任务结束。\n");
				Close();
				return false;
			}
			//判断描述符（socket）是否在集合中
			if (FD_ISSET(_sock,&fdRead)) {
				FD_CLR(_sock, &fdRead);
				Accept();
				return true;
			}
			//printf("空闲时间处理其他业务。。\n");
			return true;
		}
		return false;
	}
	//是否工作
	bool isRun() {
		return _sock != INVALID_SOCKET;
	}

	//计算并输出每秒收到的网络消息
	void time4msg() {
		auto t1 = _tTime.getElapsedSecond();
		if (t1 >=1.0) {
			printf("thread<%d>, time<%lf>, socket<%d>, clients<%d>, recvCount<%d>, msgCount<%d>\n",(int)_cellServers.size(), t1, (int)_sock, (int)_clientCount, (int)(_recvCount/t1), (int)(_msgCount / t1));
			_recvCount = 0;
			_msgCount = 0;
			_tTime.update();
		}
	}

	//只会被一个线程调用 安全
	virtual void OnNetJoin(ClientSocket* pClient) {
		_clientCount++;
	}

	//cellserver 4 多个线程触发不安全
	virtual void OnNetLeave(ClientSocket* pClient) {
		_clientCount--;
	}

	//cellserver 4 多个线程触发不安全
	virtual void OnNetMsg(CellServer* pCellServer,ClientSocket* pClient, DataHeader* header) {
		_msgCount++;
	}

	//
	virtual void OnNetRecv(ClientSocket* pClient) {
		_recvCount++;
	}
};

#endif