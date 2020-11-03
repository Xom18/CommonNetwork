#include <deque>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include "Debug.h"
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "TCPSocket.h"

void cTCPSocket::recvThread()
{
	mLOG("Begin recvThread %d", m_iPort);

	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(sockaddr_in);

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		//강제정지
		if(m_lpMasterStatus != nullptr && *m_lpMasterStatus != eTHREAD_STATUS_RUN)
			break;

		//데이터 수신
		int iDataLength = recv(m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0);

		if(iDataLength == SOCKET_ERROR)
			continue;

		//받은 데이터 처리
		cPacketTCP* pPacket = new cPacketTCP();
		pPacket->setData(iDataLength, pRecvBuffer, getSocket());

		//큐에 넣는다
		pushRecvQueue(pPacket);
	}

	pKILL(pRecvBuffer);

	mLOG("End recvThread %d", m_iPort);
}

void cTCPSocket::sendThread()//송신 스레드
{
	mLOG("Begin sendThread %d", m_iPort);

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
		std::deque<cPacketTCP*>	qSendQueue;
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
			qSendQueue.pop_front();

			//null일 일이 없으니 null체크 없이 바로
			delete pPacket;
		}
		
		if(send(m_Sock, pSendBuffer, iDataSize, 0) == SOCKET_ERROR)
		{//송신실패하면 연결 끊긴걸로 보고 정지 시작
			stop();
			continue;
		}
	}

	pKILL(pSendBuffer);
	mLOG("End sendThread %d", m_iPort);
}

//스레드 시작
bool cTCPSocket::tryConnectServer(char* _csIP, int _iPort, int _iTimeOut, bool _bUseNoDelay)
{
	if(m_pRecvThread != nullptr
	|| m_pSendThread != nullptr)
	{
		mLOG("Begin error %d", _iPort);
		return false;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(1, 0);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if(iWSOK != 0)
	{
		mLOG("Socket start error %d", _iPort);
		ExitProcess(EXIT_FAILURE);
	}

	m_iPort = _iPort;									//포트
	m_Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);	//소켓 생성
	inet_pton(AF_INET, _csIP, &m_SockInfo.sin_addr);	//특정 IP만 대상

	m_SockInfo.sin_family = AF_INET;					//TCP 사용
	m_SockInfo.sin_port = htons(_iPort);				//포트

	//노딜레이 옵션, 0아닌게 반환되면 뭐가 문제가 있다
	bool bUseNoDelay = _bUseNoDelay;
	if(setsockopt(m_Sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&bUseNoDelay, sizeof(bUseNoDelay)) != 0)
	{
		mLOG("Nodelay setting fail %d", _iPort);
		return false;
	}

	//수신 타임아웃 설정
	if(setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&_iTimeOut, sizeof(_iTimeOut)) != 0)
	{
		mLOG("Timeout setting fail %d", _iPort);
		return false;
	}

	//연결 시도
	int iErrorCode = connect(m_Sock, (sockaddr*)&m_SockInfo, sizeof(m_SockInfo));
	if(iErrorCode != 0)
	{
		mLOG("Connect error %lld", m_Sock);
		return false;
	}
	mLOG("Connect success %lld", m_Sock);

	//연결 성공했으니 스레드 시작
	begin();
	return true;
}

void cTCPSocket::begin()
{
	//상태 변경하고 스레드 시작
	m_iStatus = eTHREAD_STATUS_RUN;
	m_pRecvThread = new std::thread([&]() {recvThread(); });
	m_pSendThread = new std::thread([&]() {sendThread(); });
}

//스레드 정지
void cTCPSocket::stop()
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
	{
		m_pStoppingThread->join();
		KILL(m_pStoppingThread);
	}
	m_pStoppingThread = new std::thread([&]() {stopThread(); });
}

void cTCPSocket::stopThread()
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
		m_qSendQueue.pop_front();
		KILL(pPacket);
	}
	while(!m_qRecvQueue.empty())
	{
		cPacketTCP* pPacket = m_qRecvQueue.front();
		m_qRecvQueue.pop_front();
		KILL(pPacket);
	}

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;
}