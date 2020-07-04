//경고 썡까게 해주는 전처리
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

//메인에서 돌리는 예제 코드
int main(const int argc, const char* argv[])
{
	cUDPSocket Server;	//서버
	cUDPSocket Client;	//클라

	char IP[] = "127.0.0.1";//IP 테스트용으로 127.0.0.1

	Server.beginThread(true);
	Client.beginThread(false, IP);

	
	while (true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if (strcmp(strText.c_str(), "QUIT") == 0)
		{
			Server.stopThread();
			Client.stopThread();
			break;
		}

		//전송
		Client.pushSend(Client.getSockinfo(), strText.length() + 1, (char*)strText.c_str());

		Sleep(1000);	//패킷이 찍고오기까지 기다려주는 시간 넉넉하게 1초

		//수신받은 패킷 처리하기 위한 큐
		std::queue<cPacket*> recvQueue;
		Server.copyRecvQueue(&recvQueue, true);

		//패킷 처리
		while (!recvQueue.empty())
		{
			cPacket* lpPacket = recvQueue.front();
			recvQueue.pop();

			char clientIP[256];
			ZeroMemory(clientIP, sizeof(clientIP));
			inet_ntop(AF_INET, &lpPacket->m_AddrInfo.sin_addr, clientIP, sizeof(clientIP));
			printf("%s : %s\n", clientIP, lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}
	}

	WSACleanup();
	ExitProcess(EXIT_SUCCESS);
}