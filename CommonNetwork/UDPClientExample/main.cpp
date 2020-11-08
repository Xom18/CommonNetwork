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

cUDPSocket g_UDPClient;	//클라
void recvThread();
int main()
{
	std::string strIP;
	std::cin >> strIP;

	g_UDPClient.beginClient((char*)strIP.c_str(), _DEFAULT_PORT);
	std::thread RecvThread = std::thread([&]() {recvThread(); });

	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			g_UDPClient.stop();
			break;
		}

		g_UDPClient.pushSend((int)strText.length() + 1, (char*)strText.c_str(), g_UDPClient.getSockinfo());
	}
	RecvThread.join();
}

void recvThread()
{
	std::deque<cPacketUDP*> qRecvQueue;
	while(g_UDPClient.getSocketStatus() == eTHREAD_STATUS_RUN)
	{
		g_UDPClient.swapRecvQueue(&qRecvQueue);

		while(!qRecvQueue.empty())
		{
			cPacketUDP* lpPacket = qRecvQueue.front();
			qRecvQueue.pop_front();

			printf("[RecvMessage]%s\n", lpPacket->m_pData);

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}
	}
}