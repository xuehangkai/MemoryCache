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

	//��ʼ��socket
	void initSocket() {

#ifdef _WIN32
		//����Win Socket 2.x����
		WORD ver = MAKEWORD(2, 2);
		WSADATA dat;
		WSAStartup(ver, &dat);
#endif
		//1������һ��socket
		if (INVALID_SOCKET != _sock) {
			printf("sock=%d�رվ�����\n",(int)_sock);
			Close();
		}
		_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == _sock) {
			//printf("�����׽���socketʧ��\n");
		}
		else
		{
			//printf("�����׽���socket=%d�ɹ�\n", (int)_sock);
		}
	}
	//���ӷ�����
	int Connect(const char* ip,short port) {
		if (INVALID_SOCKET == _sock) {
			initSocket();
		}
		//printf("<socket=%d>�������ӷ�����<%s:%d>������\n", (int)_sock, ip, port);
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
			//printf("����ʧ��\n");
		}
		else {
			_isConnect = true;
			//printf("<socket=%d>���ӷ�����<%s:%d>�ɹ�\n",(int)_sock, inet_ntoa(_sin.sin_addr),port);
		}
		return ret;
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
			_sock = INVALID_SOCKET;
		}
		_isConnect = false;
	}
	//����������Ϣ
	bool OnSelect() {
		if (isRun()) {
			fd_set fdRead;
			FD_ZERO(&fdRead);
			FD_SET(_sock, &fdRead);
			timeval t = { 0,0 };
			int ret = select(_sock + 1, &fdRead, 0, 0, &t);
			if (ret < 0) {
				printf("<sock=%d>select�������1��\n", _sock);
				Close();
				return false;
			}
			if (FD_ISSET(_sock, &fdRead)) {
				FD_CLR(_sock, &fdRead);
				if (-1 == RecvData(_sock)) {
					printf("<sock=%d>select�������2��\n", _sock);
					Close();
					return false;
				}
			}
			//printf("����ʱ�䴦������ҵ�񡣡�\n");
			//Sleep(1000);
			return true;
		}
		return false;
	}
	//�Ƿ�����
	bool isRun() {
		return _sock != INVALID_SOCKET && _isConnect;
	}
#ifndef RECV_BUFF_SZIE
	//��������С��Ԫ��С
	#define RECV_BUFF_SZIE 10240
#endif
	//���ջ�����
	//char _szRecv[RECV_BUFF_SZIE] = {};
	//�ڶ������� ��Ϣ������
	char _szMsgBuf[RECV_BUFF_SZIE*5] = {};
	//��Ϣ������������β��λ��
	int _lastPos = 0;
	//�������� ����ճ�� ��ְ�
	int RecvData(SOCKET cSocket) {
		//��������

		char* szRecv = _szMsgBuf + _lastPos;
		int nLen = (int)recv(cSocket, szRecv, (RECV_BUFF_SZIE*5)- _lastPos, 0);
		if (nLen <= 0) {
			printf("��������Ͽ�����,���������\n");
			return -1;
		}
		//����ȡ�������ݿ�������Ϣ������
		//memcpy(_szMsgBuf+_lastPos, _szRecv,nLen);
		//��Ϣ������������β��λ�ú���
		_lastPos += nLen;
		//�ж���Ϣ�������������Ƿ񳤶ȴ�����ϢͷDataHeader�ĳ���
		while (_lastPos >= sizeof(DataHeader)) {
			//��ʱ�Ϳ���֪����ǰ��Ϣ�ĳ���
			DataHeader* header = (DataHeader*)_szMsgBuf;
			//�ж���Ϣ�����������ݳ��ȴ�����Ϣ����
			if (_lastPos >= header->dataLength ) {
				//��Ϣ������ʣ��δ�������ݳ���
				int nSize = _lastPos - header->dataLength;
				//����������Ϣ
				OnNetMsg(header);
				//����Ϣ������ʣ��δ��������ǰ��
				memcpy(_szMsgBuf, _szMsgBuf + header->dataLength, nSize);
				//��Ϣ������������β��λ��ǰ��
				_lastPos = nSize;
			}
			else {
				//��Ϣ������ʣ�����ݲ���һ��������Ϣ
				break;
			}
		}
		return 0;
	}
	//��Ӧ������Ϣ
	virtual void OnNetMsg(DataHeader* header) {
		switch (header->cmd)
		{
			case CMD_LOGIN_RESULT:
			{
				LoginResult* loginResult = (LoginResult*)header;
				//printf("�յ�������������Ϣ��CMD_LOGIN_RESULT�����ݳ��ȣ�%d\n", loginResult->dataLength);
			}
			break;
			case CMD_LOGINOUT_RESULT:
			{
				LoginoutResult* loginoutResult = (LoginoutResult*)header;
				//printf("�յ�������������Ϣ��CMD_LOGINOUT_RESULT�����ݳ��ȣ�%d\n", loginoutResult->dataLength);
			}
			break;
			case CMD_NEW_USER_JOIN:
			{
				NewUserJoin* userJoin = (NewUserJoin*)header;
				printf("�յ�������������Ϣ��CMD_NEW_USER_JOIN�����ݳ��ȣ�%d\n", userJoin->dataLength);
			}
			break;
			case CMD_ERROR:
			{
				printf("�յ�������������Ϣ��CMD_ERROR�����ݳ��ȣ�%d\n", header->dataLength);
			}
			break;
			default:
			{
				printf("�յ�����������δ������Ϣ�����ݳ��ȣ�%d\n", header->dataLength);
			}
		}
	}
	//��������
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