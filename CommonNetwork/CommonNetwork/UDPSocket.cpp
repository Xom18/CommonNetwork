#include <queue>
#include <mutex>
#include <WS2tcpip.h>
#include <chrono>
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "UDPSocket.h"

void cUDPSocket::recvThread()
{
	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(sockaddr_in);

	printf("Begin recvThread %d\n", m_iPort);

	while (m_iStatus == eTHREAD_STATUS_RUN)
	{
		sockaddr_in Client;
		ZeroMemory(pRecvBuffer, _MAX_PACKET_SIZE);	//초기화
		ZeroMemory(&Client, sizeof(Client));

		//데이터 수신
		int iDataLength = recvfrom((int)m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0, (sockaddr*)&Client, &ClientAddrLength);
		if (iDataLength == SOCKET_ERROR)
		{
//			printf("수신에러\n");
			continue;
		}

		//받은 데이터 처리
		cPacketUDP* pPacket = new cPacketUDP();
		pPacket->setData(iDataLength, pRecvBuffer, &Client);

		//큐에 넣는다
		pushRecvQueue(pPacket);

		//송신자 IP보려고 넣어둔거, 주석
		//char clientIP[256];
		//ZeroMemory(clientIP, sizeof(clientIP));
		//inet_ntop(AF_INET, &client.sin_addr, clientIP, sizeof(clientIP));
		//printf("%s : %s\n", clientIP, pRecvBuffer);
	}

	pKILL(pRecvBuffer);

	printf("End recvThread\n");
}

void cUDPSocket::sendThread()//송신 스레드
{
	printf("Begin sendThread %d\n", m_iPort);

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼
	while (m_iStatus == eTHREAD_STATUS_RUN)
	{
		//비어있으면 10ms동안 대기
		if (m_qSendQueue.empty())
		{
			std::chrono::milliseconds TimeMS(10);
			std::this_thread::sleep_for(TimeMS);
			continue;
		}

		//큐에 있는걸 가져온다
		std::queue<cPacketUDP*>	qSendQueue;
		{
			mAMTX(m_mtxSendMutex);
			std::swap(qSendQueue, m_qSendQueue);
		}

		ZeroMemory(pSendBuffer, _MAX_PACKET_SIZE);//데이터 초기화
		sockaddr_in AddrInfo;	//수신자 정보
		int iDataSize = 0;		//데이터 크기

		{//전송을 위해 패킷을 꺼냈다
			cPacketUDP* lpPacket = qSendQueue.front();
			iDataSize = lpPacket->m_iSize;
			memcpy(&AddrInfo, &lpPacket->m_AddrInfo, sizeof(AddrInfo));
			memcpy(pSendBuffer, lpPacket->m_pData, iDataSize);
			//누수없게 바로 해제
			qSendQueue.pop();
			delete lpPacket;
		}


		if (sendto(m_Sock, pSendBuffer, iDataSize, 0, (sockaddr*)&AddrInfo, sizeof(AddrInfo)) == SOCKET_ERROR)
		{//송신실패하면 에러
			printf("송신 에러\n");
			continue;
		}
	}

	pKILL(pSendBuffer);
	printf("End sendThread\n");
}

//스레드 시작
void cUDPSocket::beginThread(bool _bIsServer, char* _csIP, int _iPort, int _iTimeOut)
{
	if (m_pSendThread != nullptr
	|| m_pRecvThread != nullptr)
	{
		printf("이미 구동중인 스레드가 있다\n");
		return;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(1, 0);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if (iWSOK != 0)
	{
		printf("소켓 시작 에러\n");
		ExitProcess(EXIT_FAILURE);
	}

	m_iPort = _iPort;									//포트
	m_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);	//소켓 생성

	if (_csIP == nullptr)
		m_SockInfo.sin_addr.S_un.S_addr = ADDR_ANY;		//전체 대상
	else
		inet_pton(AF_INET, _csIP, &m_SockInfo.sin_addr);//특정 IP만 대상
	m_SockInfo.sin_family = AF_INET;					//UDP 사용
	m_SockInfo.sin_port = htons(m_iPort);				//포트

	//수신 타임아웃 설정
	if(setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&_iTimeOut, sizeof(_iTimeOut)) != 0)
	{
		printf("UDP 옵션 설정 실패\n");
		return;
	}

	//서버라면 바인딩 필요
	if (_bIsServer)
	{
		//바인딩, 만약 동일한 포트로 이미 열려있으면 에러남
		if (bind(m_Sock, (sockaddr*)&m_SockInfo, sizeof(sockaddr_in)) != 0)
		{
			printf("바인딩 에러[%d]\n", WSAGetLastError());
			ExitProcess(EXIT_FAILURE);
		}
	}

	//상태 변경하고 스레드 시작
	m_iStatus = eTHREAD_STATUS_RUN;
	m_pRecvThread = new std::thread([&]() {recvThread(); });
	m_pSendThread = new std::thread([&]() {sendThread(); });
}

//스레드 정지
void cUDPSocket::stopThread()
{
	//스레드가 멈춰있으면 의미없으니 return
	if (m_iStatus == eTHREAD_STATUS_IDLE
	|| m_iStatus == eTHREAD_STATUS_STOP)
		return;

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//소켓 닫음
	closesocket(m_Sock);

	//스레드 정지 대기
	m_pSendThread->join();
	m_pRecvThread->join();

	//변수 해제
	KILL(m_pSendThread);
	KILL(m_pRecvThread);

	//패킷 큐 해제
	while(!m_qSendQueue.empty())
	{
		cPacketUDP* pPacket = m_qSendQueue.front();
		m_qSendQueue.pop();
		delete pPacket;
	}

	while(!m_qRecvQueue.empty())
	{
		cPacketUDP* pPacket = m_qRecvQueue.front();
		m_qRecvQueue.pop();
		delete pPacket;
	}

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;

//	WSACleanup();
}