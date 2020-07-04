//��� ����� ���ִ� ��ó��
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include "Define.h"
#include "Macro.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "UDPSocket.h"

#pragma comment(lib, "ws2_32.lib")

//���ο��� ������ ���� �ڵ�
int main(const int argc, const char* argv[])
{
	cUDPSocket Server;	//����
	cUDPSocket Client;	//Ŭ��

	char IP[] = "127.0.0.1";//IP �׽�Ʈ������ 127.0.0.1

	Server.beginThread(true);
	Client.beginThread(false, IP);

	
	while (true)
	{
		std::string strText;
		std::cin >> strText;	//�ؽ�Ʈ �Է�

		//QUIT�� ġ�� ������ ����
		if (strcmp(strText.c_str(), "QUIT") == 0)
		{
			Server.stopThread();
			Client.stopThread();
			break;
		}

		//����
		Client.pushSend(Client.getSockinfo(), strText.length() + 1, (char*)strText.c_str());

		Sleep(1000);	//��Ŷ�� ��������� ��ٷ��ִ� �ð� �˳��ϰ� 1��

		//���Ź��� ��Ŷ ó���ϱ� ���� ť
		std::queue<cPacket*> recvQueue;
		Server.copyRecvQueue(&recvQueue, true);

		//��Ŷ ó��
		while (!recvQueue.empty())
		{
			cPacket* lpPacket = recvQueue.front();
			recvQueue.pop();

			char clientIP[256];
			ZeroMemory(clientIP, sizeof(clientIP));
			inet_ntop(AF_INET, &lpPacket->m_AddrInfo.sin_addr, clientIP, sizeof(clientIP));
			printf("%s : %s\n", clientIP, lpPacket->m_pData);

			//�ݵ�� ó���� �� ��Ŷ delete
			delete lpPacket;
		}
	}

	WSACleanup();
	ExitProcess(EXIT_SUCCESS);
}