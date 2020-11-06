#include <iostream>
#include "CommonNetwork.h"

#ifdef _WIN64
	#ifdef _DEBUG
		#pragma comment(lib, "../x64/Debug/CommonNetwork.lib")
	#else
		#pragma comment(lib, "../x64/Release/CommonNetwork.lib")
	#endif
#else
	#ifdef _DEBUG
		#pragma comment(lib, "../Debug/CommonNetwork.lib")
	#else
		#pragma comment(lib, "../Release/CommonNetwork.lib")
	#endif
#endif

#pragma comment(lib, "ws2_32.lib")

cTCPSocket g_TCPClient;	//클라
void recvThread();
int main()
{
	std::string strIP;
	std::cin >> strIP;

	g_TCPClient.tryConnectServer((char*)strIP.c_str());
	std::thread RecvThread = std::thread([&]() {recvThread(); });

	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			g_TCPClient.stop();
			break;
		}

		g_TCPClient.pushSend((int)strText.length() + 1, (char*)strText.c_str());
	}

	RecvThread.join();
}

void recvThread()
{
	std::deque<cPacketTCP*> qRecvQueue;
	while(g_TCPClient.getSocketStatus() == eTHREAD_STATUS_RUN)
	{
		g_TCPClient.swapRecvQueue(&qRecvQueue);

		while(!qRecvQueue.empty())
		{
			cPacketTCP* lpPacket = qRecvQueue.front();
			qRecvQueue.pop_front();

			printf("[Client]%lld : %s\n", lpPacket->m_Sock, lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}
		Sleep(15);
	}
}