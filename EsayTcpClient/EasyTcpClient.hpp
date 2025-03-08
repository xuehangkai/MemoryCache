#ifndef _EasyTcpClient_hpp_
#define _EasyTcpClient_hpp_

#ifdef _WIN32
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
#include "MessageHeader.hpp"

class EasyTcpClient
{
private:
	SOCKET _sock;
	bool _isConnect;
public:
	EasyTcpClient() {
		_sock = INVALID_SOCKET;
		_isConnect = false;
	}

	virtual ~EasyTcpClient() {
		Close();
	}

	//初始化socket
	void initSocket() {

#ifdef _WIN32
		//启动Win Socket 2.x环境
		WORD ver = MAKEWORD(2, 2);
		WSADATA dat;
		WSAStartup(ver, &dat);
#endif
		//1、建立一个socket
		if (INVALID_SOCKET != _sock) {
			printf("sock=%d关闭旧连接\n",(int)_sock);
			Close();
		}
		_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == _sock) {
			//printf("建立套接字socket失败\n");
		}
		else
		{
			//printf("建立套接字socket=%d成功\n", (int)_sock);
		}
	}
	//连接服务器
	int Connect(const char* ip,short port) {
		if (INVALID_SOCKET == _sock) {
			initSocket();
		}
		//printf("<socket=%d>正在连接服务器<%s:%d>。。。\n", (int)_sock, ip, port);
		sockaddr_in _sin = {};
		_sin.sin_family = AF_INET;
		_sin.sin_port = htons(port);//host to net unsigned short
#ifdef	_WIN32
		_sin.sin_addr.S_un.S_addr = inet_addr(ip);
#else
		_sin.sin_addr.s_addr = inet_addr(ip);
#endif
		int ret = connect(_sock, (sockaddr*)&_sin, sizeof(sockaddr_in));
		if (SOCKET_ERROR == ret) {
			//printf("连接失败\n");
		}
		else {
			_isConnect = true;
			//printf("<socket=%d>连接服务器<%s:%d>成功\n",(int)_sock, inet_ntoa(_sin.sin_addr),port);
		}
		return ret;
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
			_sock = INVALID_SOCKET;
		}
		_isConnect = false;
	}
	//处理网络消息
	bool OnSelect() {
		if (isRun()) {
			fd_set fdRead;
			FD_ZERO(&fdRead);
			FD_SET(_sock, &fdRead);
			timeval t = { 0,0 };
			int ret = select(_sock + 1, &fdRead, 0, 0, &t);
			if (ret < 0) {
				printf("<sock=%d>select任务结束1。\n", _sock);
				Close();
				return false;
			}
			if (FD_ISSET(_sock, &fdRead)) {
				FD_CLR(_sock, &fdRead);
				if (-1 == RecvData(_sock)) {
					printf("<sock=%d>select任务结束2。\n", _sock);
					Close();
					return false;
				}
			}
			//printf("空闲时间处理其他业务。。\n");
			//Sleep(1000);
			return true;
		}
		return false;
	}
	//是否工作中
	bool isRun() {
		return _sock != INVALID_SOCKET && _isConnect;
	}
#ifndef RECV_BUFF_SZIE
	//缓冲区最小单元大小
	#define RECV_BUFF_SZIE 10240
#endif
	//接收缓冲区
	//char _szRecv[RECV_BUFF_SZIE] = {};
	//第二缓冲区 消息缓冲区
	char _szMsgBuf[RECV_BUFF_SZIE*5] = {};
	//消息缓冲区的数据尾部位置
	int _lastPos = 0;
	//接收数据 处理粘包 拆分包
	int RecvData(SOCKET cSocket) {
		//接收数据

		char* szRecv = _szMsgBuf + _lastPos;
		int nLen = (int)recv(cSocket, szRecv, (RECV_BUFF_SZIE*5)- _lastPos, 0);
		if (nLen <= 0) {
			printf("与服务器断开连接,任务结束。\n");
			return -1;
		}
		//将收取到的数据拷贝到消息缓冲区
		//memcpy(_szMsgBuf+_lastPos, _szRecv,nLen);
		//消息缓冲区的数据尾部位置后移
		_lastPos += nLen;
		//判断消息缓冲区的数据是否长度大于消息头DataHeader的长度
		while (_lastPos >= sizeof(DataHeader)) {
			//这时就可以知道当前消息的长度
			DataHeader* header = (DataHeader*)_szMsgBuf;
			//判断消息缓冲区的数据长度大于消息长度
			if (_lastPos >= header->dataLength ) {
				//消息缓冲区剩余未处理数据长度
				int nSize = _lastPos - header->dataLength;
				//处理网络消息
				OnNetMsg(header);
				//将消息缓冲区剩余未处理数据前移
				memcpy(_szMsgBuf, _szMsgBuf + header->dataLength, nSize);
				//消息缓冲区的数据尾部位置前移
				_lastPos = nSize;
			}
			else {
				//消息缓冲区剩余数据不够一条完整消息
				break;
			}
		}
		return 0;
	}
	//响应网络消息
	virtual void OnNetMsg(DataHeader* header) {
		switch (header->cmd)
		{
			case CMD_LOGIN_RESULT:
			{
				LoginResult* loginResult = (LoginResult*)header;
				//printf("收到服务器返回消息：CMD_LOGIN_RESULT，数据长度：%d\n", loginResult->dataLength);
			}
			break;
			case CMD_LOGINOUT_RESULT:
			{
				LoginoutResult* loginoutResult = (LoginoutResult*)header;
				//printf("收到服务器返回消息：CMD_LOGINOUT_RESULT，数据长度：%d\n", loginoutResult->dataLength);
			}
			break;
			case CMD_NEW_USER_JOIN:
			{
				NewUserJoin* userJoin = (NewUserJoin*)header;
				printf("收到服务器返回消息：CMD_NEW_USER_JOIN，数据长度：%d\n", userJoin->dataLength);
			}
			break;
			case CMD_ERROR:
			{
				printf("收到服务器返回消息：CMD_ERROR，数据长度：%d\n", header->dataLength);
			}
			break;
			default:
			{
				printf("收到服务器返回未定义消息，数据长度：%d\n", header->dataLength);
			}
		}
	}
	//发送数据
	int SendData(DataHeader* header,int nLen) {
		int ret = SOCKET_ERROR;
		if (isRun()&& header) {
			return send(_sock, (const char*)header, nLen, 0);
			if (SOCKET_ERROR==ret) {
				Close();
			}
		}
		return ret;
	}
};
#endif