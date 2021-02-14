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

enum
{
	ePACKET_TYPE_MESSAGE,
};

#pragma pack(push, 1)
//메세지 패킷
struct stPacketMessage : stPacketBase
{
	char m_aText[512];

	void resize()
	{
		m_iSize = static_cast<UINT32>(sizeof(stPacketBase) + strnlen_s(m_aText, sizeof(m_aText)) + 1);
	}
};
#pragma pack(pop)

cTCPServer g_TCPServer;	//서버
void recvThread();
int main()
{
	std::string strNum;
	std::cin >> strNum;

	//6적으면 ipv6 4적으면 ipv4 다른거적으면 둘다
	if(strcmp(strNum.c_str(), "6") == 0)
		g_TCPServer.begin(_DEFAULT_PORT, eWINSOCK_USE_IPv6);
	else if(strcmp(strNum.c_str(), "4") == 0)
		g_TCPServer.begin(_DEFAULT_PORT, eWINSOCK_USE_IPv4);
	else
		g_TCPServer.begin();

	std::thread RecvThread = std::thread([&]() {recvThread(); });

	while(true)
	{
		std::string strText;
		std::cin >> strText;	//텍스트 입력

		//QUIT을 치면 스레드 종료
		if(strcmp(strText.c_str(), "QUIT") == 0)
		{
			g_TCPServer.stop();
			break;
		}
	}
	RecvThread.join();
}

void recvThread()
{
	std::deque<cPacketTCP*> qRecvQueue;
	while(g_TCPServer.getSocketStatus() == eTHREAD_STATUS_RUN)
	{
		g_TCPServer.swapRecvQueue(&qRecvQueue);

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

				//클라이언트 핑퐁 테스트용
				g_TCPServer.sendPacket(lpPacket->m_iIndex, lpPacket->m_iSize, lpPacket->m_pData);
				break;
			}
			}

			//반드시 처리한 뒤 패킷 delete
			delete lpPacket;
		}

		Sleep(15);
	}
}