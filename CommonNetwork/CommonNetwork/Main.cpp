//경고 무시하게 해주는 전처리
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
#include "TCPSocket.h"
#include "TCPSocketServer.h"

#pragma comment(lib, "ws2_32.lib")

char IP[] = "127.0.0.1";//IP 테스트용으로 127.0.0.1

void TCPTest();
void UDPTest();
//메인에서 돌리는 예제 코드
int main(const int argc, const char* argv[])
{
	std::string strText;
	std::cin >> strText;	//텍스트 입력

	//QUIT을 치면 스레드 종료
	if(strcmp(strText.c_str(), "TCP") == 0)
	{
		TCPTest();
	}
	else if(strcmp(strText.c_str(), "UDP") == 0)
	{
		UDPTest();
	}


	WSACleanup();
	ExitProcess(EXIT_SUCCESS);
}

void TCPTest()
{
	cTCPSocketServer TCPServer;	//서버
	cTCPSocket TCPClient;	//클라

	TCPServer.beginThread();
//	TCPClient.tryConnectServer(IP);

	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			TCPServer.stopThread();
			TCPServer.stopThread();

			break;
		}
	}
}

void UDPTest()
{
	cUDPSocket UDPServer;	//서버
	cUDPSocket UDPClient;	//클라

	UDPServer.beginThread(true);
	UDPClient.beginThread(false, IP);


	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			UDPServer.stopThread();
			UDPClient.stopThread();

			break;
		}

		//전송
		UDPClient.pushSend(UDPClient.getSockinfo(), strText.length() + 1, (char*)strText.c_str());
		
		Sleep(1000);	//패킷이 찍고오기까지 기다려주는 시간 넉넉하게 1초

		//수신받은 패킷 처리하기 위한 큐
		std::queue<cPacket*> recvQueue;
		UDPServer.copyRecvQueue(&recvQueue, true);

		//패킷 처리
		while(!recvQueue.empty())
		{
			cPacket* lpPacket = recvQueue.front();
			recvQueue.pop();

			char clientIP[256];
			ZeroMemory(clientIP, sizeof(clientIP));
			inet_ntop(AF_INET, &lpPacket->m_AddrInfo.sin_addr, clientIP, sizeof(clientIP));
			printf("[Server]%s : %s\n", clientIP, lpPacket->m_pData);

			UDPServer.pushSend(&lpPacket->m_AddrInfo, lpPacket->m_iSize, lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}

		Sleep(1000);	//패킷이 찍고오기까지 기다려주는 시간 넉넉하게 1초

		UDPClient.copyRecvQueue(&recvQueue, true);
		while(!recvQueue.empty())
		{
			cPacket* lpPacket = recvQueue.front();
			recvQueue.pop();

			char clientIP[256];
			ZeroMemory(clientIP, sizeof(clientIP));
			inet_ntop(AF_INET, &lpPacket->m_AddrInfo.sin_addr, clientIP, sizeof(clientIP));
			printf("[Client]%s : %s\n", clientIP, lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}
	}
}