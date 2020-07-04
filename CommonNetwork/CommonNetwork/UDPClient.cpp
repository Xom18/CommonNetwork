#include <queue>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "UDPClient.h"

void cUDPClient::commitSendWaitQueue()
{
	//혹시 송신대기로 올리는 중인게 있을 수 있으니 락
	mAMTX(m_mtxSendMutex);
	if (m_qSendWaitQueue.empty())
		return;
	std::swap(m_qSendQueue, m_qSendWaitQueue);
}

void cUDPClient::recvThread()
{
	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];
	sockaddr_in client;
	int iClientLength = sizeof(client);

	printf("Begin recvThread %d\n", m_iPort);

	while (m_iStatus == eSOCKET_STATUS_RUN)
	{
		ZeroMemory(pRecvBuffer, _MAX_PACKET_SIZE);	//초기화
		ZeroMemory(&client, iClientLength);

		//데이터 수신
		int iDataLength = recvfrom(m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0, (sockaddr*)&client, &iClientLength);
		if (iDataLength == SOCKET_ERROR)
		{
			printf("수신에러\n");
			continue;
		}

		//받은 데이터 처리
		cPacket* pPacket = new cPacket();
		pPacket->setData(&client, iDataLength, pRecvBuffer);

		//큐에 넣는다
		pushRecvQueue(pPacket);

		//송신자 IP보려고 넣어둔거, 주석
		//char clientIP[256];
		//ZeroMemory(clientIP, sizeof(clientIP));
		//inet_ntop(AF_INET, &client.sin_addr, clientIP, sizeof(clientIP));
		//printf("%s\n", clientIP);
	}

	pKILL(pRecvBuffer);

	printf("End recvThread\n");
}

void cUDPClient::pushRecvQueue(cPacket* _lpPacket)
{
	mAMTX(m_mtxSendMutex);
	m_qRecvQueue.push(_lpPacket);
}

void cUDPClient::sendThread()//송신 스레드
{
	printf("Begin sendThread %d\n", m_iPort);

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼
	while (m_iStatus == eSOCKET_STATUS_RUN)
	{
		if (m_qSendQueue.empty())
		{//비어있으면 대기큐꺼 가져온다
			commitSendWaitQueue();

			//대기큐꺼도 비어있으면 10ms동안 대기
			if (m_qSendQueue.empty())
				Sleep(10);
			continue;
		}

		ZeroMemory(pSendBuffer, _MAX_PACKET_SIZE);//데이터 초기화
		sockaddr_in AddrInfo;	//수신자 정보
		int iDataSize = 0;		//데이터 크기

		{//전송을 위해 패킷을 꺼냈다
			cPacket* lpPacket = m_qSendQueue.front();
			iDataSize = lpPacket->m_iSize;
			memcpy(&AddrInfo, &lpPacket->m_AddrInfo, sizeof(AddrInfo));
			memcpy(pSendBuffer, &lpPacket->m_pData, iDataSize);
			//누수없이 바로 해제
			m_qSendQueue.pop();
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
void cUDPClient::beginThread(bool _bIsServer, char* _csIP, int _iPort)
{
	if (m_pSendThread != nullptr
	|| m_pRecvThread != nullptr)
	{
		printf("이미 구동중인 스레드가 있다\n");
		return;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(1, 0);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소캣 시작
	if (iWSOK != 0)
	{
		printf("소캣 시작 에러\n");
		ExitProcess(EXIT_FAILURE);
	}

	m_iPort = _iPort;									//포트
	m_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);	//소캣 생성

	if(_csIP == nullptr)
		m_SockInfo.sin_addr.S_un.S_addr = ADDR_ANY;		//전체 대상
	else
		inet_pton(AF_INET, _csIP, &m_SockInfo.sin_addr);//특정 IP만 대상
	m_SockInfo.sin_family = AF_INET;					//UDP 사용
	m_SockInfo.sin_port = htons(m_iPort);				//포트

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
	m_iStatus = eSOCKET_STATUS_RUN;
	m_pRecvThread = new std::thread([&]() {recvThread(); });
	m_pSendThread = new std::thread([&]() {sendThread(); });
}

//스레드 정지
void cUDPClient::stopThread()
{
	//스레드가 멈춰있으면 의미없으니 return
	if (m_iStatus == eSOCKET_STATUS_IDLE)
		return;

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eSOCKET_STATUS_STOP;

	//스레드 정지 대기
	m_pSendThread->join();
	m_pRecvThread->join();

	//변수 해제
	KILL(m_pSendThread);
	KILL(m_pSendThread);

	//상태 재설정
	m_iStatus = eSOCKET_STATUS_IDLE;

	//소캣 닫음
	closesocket(m_Sock);

	//	WSACleanup();
}

//송신큐에 넣어놓기
void cUDPClient::pushSend(cPacket* _lpPacket)
{
	//UDP는 패킷 크기가 커질수록 도착할 확률이 낮아져서 일부러 작게함
	if (_lpPacket->m_iSize >= _MAX_UDP_DATA_SIZE)
	{
		printf("패킷 크기 너무 큼 %d\n", _lpPacket->m_iSize);
		return;
	}

	m_qSendWaitQueue.push(_lpPacket);
}

//수신큐 제일 앞 내용물
cPacket* cUDPClient::frontRecvQueue()
{
	mAMTX(m_mtxSendMutex);
	if (m_qRecvQueue.empty())
		return nullptr;

	cPacket* Packet = m_qRecvQueue.front();
	return Packet;
}

//수신큐 제일 앞 내용물 꺼내기
void cUDPClient::popRecvQueue()
{
	mAMTX(m_mtxSendMutex);
	if (m_qRecvQueue.empty())
		return;

	m_qRecvQueue.pop();
}

//수신 큐 초기화
void cUDPClient::resetRecvQueue()
{
	mAMTX(m_mtxRecvMutex);
	if (m_qRecvQueue.empty())
		return;

	std::queue<cPacket*>	qRecvQueue;
	std::swap(m_qRecvQueue, qRecvQueue);
}

//수신 큐 내용물 복사
void cUDPClient::copyRecvQueue(std::queue<cPacket*>* _lpQueue)
{
	mAMTX(m_mtxRecvMutex);
	*_lpQueue = m_qRecvQueue;
}