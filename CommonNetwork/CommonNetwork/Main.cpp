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


	std::cin >> strText;	//텍스트 입력
	ExitProcess(EXIT_SUCCESS);
}

void TCPTest()
{
	const int iDummyCount = 100;	//테스트용 더미
	cTCPSocketServer TCPServer;	//서버
	cTCPSocket TCPClient;	//클라

	TCPServer.begin();

	cTCPSocket aTCPClient[iDummyCount];	//클라

	for(int i = 0; i < iDummyCount; ++i)
	{
		aTCPClient[i].tryConnectServer(IP);
	}

	TCPClient.tryConnectServer(IP);

	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			TCPServer.stop();
			TCPClient.stop();
			for(int i = 0; i < iDummyCount; ++i)
				aTCPClient[i].stop();

			break;
		}

		TCPClient.pushSend((int)strText.length() + 1, (char*)strText.c_str());

		Sleep(1000);	//패킷이 찍고오기까지 기다려주는 시간 넉넉하게 1초

		//수신받은 패킷 처리하기 위한 큐
		std::deque<cPacketTCP*> recvQueue;
		TCPServer.getRecvQueue(&recvQueue);

		//패킷 처리
		while(!recvQueue.empty())
		{
			cPacketTCP* lpPacket = recvQueue.front();
			recvQueue.pop_front();

			printf("[Server]%lld : %s\n", lpPacket->m_Sock, lpPacket->m_pData);

			TCPServer.sendAll(lpPacket->m_iSize, lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}

		Sleep(1000);	//패킷이 찍고오기까지 기다려주는 시간 넉넉하게 1초

		TCPClient.getRecvQueue(&recvQueue);
		while(!recvQueue.empty())
		{
			cPacketTCP* lpPacket = recvQueue.front();
			recvQueue.pop_front();

			printf("[Client]%lld : %s\n", lpPacket->m_Sock, lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}

		for(int i = 0; i < iDummyCount; ++i)
		{
			aTCPClient[i].getRecvQueue(&recvQueue);
			while(!recvQueue.empty())
			{
				cPacketTCP* lpPacket = recvQueue.front();
				recvQueue.pop_front();

				printf("[Client]%lld : %s\n", lpPacket->m_Sock, lpPacket->m_pData);

				//반드시 처리한 뒤 패킷 delete
				delete lpPacket;
			}
		}
	}
}

void UDPTest()
{
	cUDPSocket UDPServer;	//서버
	cUDPSocket UDPClient;	//클라

	UDPServer.begin(true);
	UDPClient.begin(false, IP);


	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			UDPServer.stop();
			UDPClient.stop();

			break;
		}

		//전송
		UDPClient.pushSend((int)strText.length() + 1, (char*)strText.c_str(), UDPClient.getSockinfo());
		
		Sleep(1000);	//패킷이 찍고오기까지 기다려주는 시간 넉넉하게 1초

		//수신받은 패킷 처리하기 위한 큐
		std::deque<cPacketUDP*> recvQueue;
		if(!UDPServer.getRecvQueue(&recvQueue))
			continue;

		//패킷 처리
		while(!recvQueue.empty())
		{
			cPacketUDP* lpPacket = recvQueue.front();
			recvQueue.pop_front();

			char clientIP[256];
			ZeroMemory(clientIP, sizeof(clientIP));
			inet_ntop(AF_INET, &lpPacket->m_AddrInfo.sin_addr, clientIP, sizeof(clientIP));
			printf("[Server]%s : %s\n", clientIP, lpPacket->m_pData);

			UDPServer.pushSend(lpPacket->m_iSize, lpPacket->m_pData, &lpPacket->m_AddrInfo);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}

		Sleep(1000);	//패킷이 찍고오기까지 기다려주는 시간 넉넉하게 1초

		UDPClient.getRecvQueue(&recvQueue);
		while(!recvQueue.empty())
		{
			cPacketUDP* lpPacket = recvQueue.front();
			recvQueue.pop_front();

			char clientIP[256];
			ZeroMemory(clientIP, sizeof(clientIP));
			inet_ntop(AF_INET, &lpPacket->m_AddrInfo.sin_addr, clientIP, sizeof(clientIP));
			printf("[Client]%s : %s\n", clientIP, lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}
	}
}