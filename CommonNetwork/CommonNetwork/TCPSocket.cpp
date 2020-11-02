#include <queue>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "TCPSocket.h"

void cTCPSocket::recvThread()
{
	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(sockaddr_in);

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		//강제정지
		if(m_lpMasterStatus != nullptr && *m_lpMasterStatus != eTHREAD_STATUS_RUN)
			break;

		sockaddr_in Client;
		ZeroMemory(pRecvBuffer, _MAX_PACKET_SIZE);	//초기화
		ZeroMemory(&Client, sizeof(Client));

		//데이터 수신
		int iDataLength = recvfrom((int)m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0, (sockaddr*)&Client, &ClientAddrLength);
		if(iDataLength == SOCKET_ERROR)
		{
			//printf("수신에러\n");
			continue;
		}

		//받은 데이터 처리
		cPacketTCP* pPacket = new cPacketTCP();
		pPacket->setData(iDataLength, pRecvBuffer, getSocket());

		//큐에 넣는다
		pushRecvQueue(pPacket);

	}

	pKILL(pRecvBuffer);

	printf("End recvThread\n");
}

void cTCPSocket::sendThread()//송신 스레드
{
	printf("Begin sendThread\n");

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼
	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		//강제정지
		if(m_lpMasterStatus != nullptr && *m_lpMasterStatus != eTHREAD_STATUS_RUN)
			break;

		//송신 큐가 비어있다
		if(m_qSendQueue.empty())
		{
			std::chrono::milliseconds TimeMS(10);
			std::this_thread::sleep_for(TimeMS);
			continue;
		}
		
		//큐에 있는걸 가져온다
		std::queue<cPacketTCP*>	qSendQueue;
		{
			mAMTX(m_mtxSendMutex);
			std::swap(qSendQueue, m_qSendQueue);
		}

		ZeroMemory(pSendBuffer, _MAX_PACKET_SIZE);//데이터 초기화
		sockaddr_in AddrInfo;	//수신자 정보
		int iDataSize = 0;		//데이터 크기

		{//전송을 위해 패킷을 꺼냈다
			cPacketTCP* pPacket = qSendQueue.front();
			iDataSize = pPacket->m_iSize;
			memcpy(&AddrInfo, &pPacket->m_Sock, sizeof(AddrInfo));
			memcpy(pSendBuffer, pPacket->m_pData, iDataSize);
			//누수없게 바로 해제
			qSendQueue.pop();

			//null일 일이 없으니 null체크 없이 바로
			delete pPacket;
		}
		
		if(send(m_Sock, pSendBuffer, iDataSize, 0) == SOCKET_ERROR)
		{//송신실패하면 에러
			printf("송신 에러\n");
			continue;
		}
	}

	pKILL(pSendBuffer);
	printf("End sendThread\n");
}

//스레드 시작
bool cTCPSocket::tryConnectServer(char* _csIP, int _iPort, int _iTimeOut, bool _bUseNoDelay)
{
	if(m_pRecvThread != nullptr
	|| m_pSendThread != nullptr)
	{
		printf("이미 구동중인 스레드가 있다\n");
		return false;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(1, 0);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if(iWSOK != 0)
	{
		printf("소켓 시작 에러\n");
		ExitProcess(EXIT_FAILURE);
	}

//	m_iPort = _iPort;									//포트
	m_Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);	//소켓 생성
	inet_pton(AF_INET, _csIP, &m_SockInfo.sin_addr);	//특정 IP만 대상

	m_SockInfo.sin_family = AF_INET;					//TCP 사용
	m_SockInfo.sin_port = htons(_iPort);				//포트

	//노딜레이 옵션, 0아닌게 반환되면 뭐가 문제가 있다
	bool bUseNoDelay = _bUseNoDelay;
	if(setsockopt(m_Sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&bUseNoDelay, sizeof(bUseNoDelay)) != 0)
	{
		printf("TCP 옵션 설정 실패\n");
		return false;
	}

	//수신 타임아웃 설정
	if(setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&_iTimeOut, sizeof(_iTimeOut)) != 0)
	{
		printf("TCP 옵션 설정 실패\n");
		return false;
	}

	//연결 시도
	int iErrorCode = connect(m_Sock, (sockaddr*)&m_SockInfo, sizeof(m_SockInfo));
	if(iErrorCode != 0)
	{
		printf("TCP 연결 실패(%d) [%s : %d]\n", iErrorCode, _csIP, _iPort);
		return false;
	}

	//연결 성공했으면 스레드 시작
	printf("TCP Connect [%s : %d]\n", _csIP, _iPort);

	beginThread();
	return true;
}

//스레드 시작
void cTCPSocket::beginThread()
{
	if(m_Sock == 0)
		return;

	//상태 변경하고 스레드 시작
	m_iStatus = eTHREAD_STATUS_RUN;
	m_pRecvThread = new std::thread([&]() {recvThread(); });
	m_pSendThread = new std::thread([&]() {sendThread(); });
}

//스레드 정지
void cTCPSocket::stopThread()
{
	//스레드가 멈춰있으면 의미없으니 return
	if(m_iStatus == eTHREAD_STATUS_IDLE
	|| m_iStatus == eTHREAD_STATUS_STOP)
		return;

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//소켓 닫음
	closesocket(m_Sock);

	//중단처리 스레드
	if(m_pStoppingThread != nullptr)
		KILL(m_pStoppingThread);
	m_pStoppingThread = new std::thread([&]() {stoppingThread(); });
}

void cTCPSocket::stoppingThread()
{
	//스레드 처리
	m_pSendThread->join();
	m_pRecvThread->join();

	//변수 해제
	KILL(m_pSendThread);
	KILL(m_pRecvThread);

	while(!m_qSendQueue.empty())
	{
		cPacketTCP* pPacket = m_qSendQueue.front();
		m_qSendQueue.pop();
		KILL(pPacket);
	}
	while(!m_qRecvQueue.empty())
	{
		cPacketTCP* pPacket = m_qRecvQueue.front();
		m_qRecvQueue.pop();
		KILL(pPacket);
	}

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;
}