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

cUDPSocket g_UDPServer;	//클라
void recvThread();
int main()
{
	g_UDPServer.begin(true);
	std::thread m_RecvThread = std::thread([&]() {recvThread(); });

	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			g_UDPServer.stop();
			break;
		}
	}
}

void recvThread()
{
	std::deque<cPacketUDP*> qRecvQueue;
	while(g_UDPServer.getSocketStatus() == eTHREAD_STATUS_RUN)
	{
		g_UDPServer.swapRecvQueue(&qRecvQueue);

		while(!qRecvQueue.empty())
		{
			cPacketUDP* lpPacket = qRecvQueue.front();
			qRecvQueue.pop_front();

			printf("[RecvMessage]%lld : %s\n", lpPacket->m_AddrInfo.sin_port, lpPacket->m_pData);
			g_UDPServer.pushSend(lpPacket->m_iSize, lpPacket->m_pData, &lpPacket->m_AddrInfo);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}
	}
}