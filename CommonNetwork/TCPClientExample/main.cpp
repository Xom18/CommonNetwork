#include <iostream>
#include <string>
#include "CommonNetwork.h"
#include "ExamplePacket.h"

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

cTCPClient g_TCPClient;	//클라
void recvThread(cTCPClient* _lpClient);
int main()
{
	std::string strIP;
	std::cin >> strIP;
	char csText[512];
	memcpy(csText, strIP.c_str(), strIP.size());


	g_TCPClient.tryConnectServer(strIP.c_str());
	std::thread RecvThread = std::thread([&]() {recvThread(&g_TCPClient); });

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

		//패킷 유형 정의, 데이터적용, 전송
		stPacketMessage MessagePacket;
		MessagePacket.setInfo(ePACKET_TYPE_MESSAGE, sizeof(MessagePacket));
		strcpy_s(MessagePacket.m_aText, strText.c_str());
		MessagePacket.resize();

		g_TCPClient.pushSend(MessagePacket.m_iSize, (char*)&MessagePacket);
	}

	RecvThread.join();
}

void recvThread(cTCPClient* _lpClient)
{
	std::deque<cPacketTCP*> qRecvQueue;
	while(_lpClient->getSocketStatus() == eTHREAD_STATUS_RUN)
	{
		_lpClient->swapRecvQueue(&qRecvQueue);

		while(!qRecvQueue.empty())
		{
			cPacketTCP* lpPacket = qRecvQueue.front();
			qRecvQueue.pop_front();
			stPacketBase* lpPacketInfo = reinterpret_cast<stPacketBase*>(lpPacket->m_pData);

			//각 유형에 맞게 패킷 처리하는 부분
			switch (lpPacketInfo->m_iType)
			{
			case ePACKET_TYPE_MESSAGE:
			{
				stPacketMessage* lpPacketData = reinterpret_cast<stPacketMessage*>(lpPacketInfo);
				printf("[RecvMessage]%s\n", lpPacketData->m_aText);

				break;
			}
			}

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}
		Sleep(15);
	}
}