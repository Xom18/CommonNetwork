#include <deque>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include <condition_variable>
#include "Debug.h"
#include "Macro.h"
#include "Define.h"
#include "Packet.h"
#include "TCPClient.h"

void cTCPClient::recvThread()
{
	mLOG("Begin recvThread");

	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(sockaddr_in);

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		//데이터 수신
		int iDataLength = recv(m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0);

		if (iDataLength == 0)
		{
			stop();
			continue;
		}

		if(iDataLength == SOCKET_ERROR)
			continue;

		//받은 데이터 처리
		cPacketTCP* pPacket = new cPacketTCP();
		pPacket->setData(iDataLength, pRecvBuffer);

		//큐에 넣는다
		pushRecvQueue(pPacket);
	}

	pKILL(pRecvBuffer);

	mLOG("End recvThread");
}

void cTCPClient::sendThread()//송신 스레드
{
	mLOG("Begin sendThread");

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼
	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		//송신 큐가 비어있다, 재워준다
		if(m_qSendQueue.empty())
		{
			std::mutex mtxWaiter;
			std::unique_lock<std::mutex> lkWaiter(mtxWaiter);
			m_cvWaiter.wait(lkWaiter, [&] {
				return !m_qSendQueue.empty() || m_iStatus != eTHREAD_STATUS_RUN;
			});
			lkWaiter.unlock();

			if(m_iStatus != eTHREAD_STATUS_RUN)
				break;
		}

		//큐에 있는걸 가져온다
		std::deque<cPacketTCP*>	qSendQueue;
		{
			mLG(m_mtxSendMutex);
			std::swap(qSendQueue, m_qSendQueue);
		}

		while(!qSendQueue.empty())
		{
			ZeroMemory(pSendBuffer, _MAX_PACKET_SIZE);//데이터 초기화
			int iSendSize = 0;		//데이터 크기

			//전송을 위해 패킷을 꺼냈다
			cPacketTCP* pPacket = qSendQueue.front();

			iSendSize = pPacket->m_iSize;
			memcpy(pSendBuffer, pPacket->m_pData, iSendSize);
			qSendQueue.pop_front();
			delete pPacket;
			
			if(send(m_Sock, pSendBuffer, iSendSize, 0) == SOCKET_ERROR)
			{
				//전송 실패하면 연결이 끊긴걸로 보고 정지한다
				while(!qSendQueue.empty())
				{//정지 전 누수없이 삭제
					cPacketTCP* pTempPacket = qSendQueue.front();
					qSendQueue.pop_front();
					delete pTempPacket;
				}
				stop();
				break;
			}
		}
	}

	pKILL(pSendBuffer);
	mLOG("End sendThread");
}

//스레드 시작
bool cTCPClient::tryConnectServer(const char* _csIP, const char* _csPort, int _iTimeOut, bool _bUseNoDelay)
{
	if(m_pRecvThread != nullptr
	|| m_pSendThread != nullptr)
	{
		mLOG("Begin error %s", _csPort);
		return false;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(2, 2);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if(iWSOK != 0)
	{
		mLOG("Socket start error %s", _csPort);
		ExitProcess(EXIT_FAILURE);
	}

	addrinfo addrIn;
	addrinfo* addrRes;

	memset(&addrIn, 0, sizeof(addrIn));
	addrIn.ai_family = AF_UNSPEC;
	addrIn.ai_socktype = SOCK_STREAM;
	addrIn.ai_flags = AI_PASSIVE;

	getaddrinfo(_csIP, _csPort, &addrIn, &addrRes);
	if (addrRes == nullptr)
	{
		mLOG("Socket start error %d", WSAGetLastError());
		return false;
	}
	m_Sock = socket(addrRes->ai_family, addrRes->ai_socktype, addrRes->ai_protocol);
	memcpy(&m_SockInfo, addrRes->ai_addr, sizeof(m_SockInfo));

	freeaddrinfo(addrRes);

	//노딜레이 옵션, 0아닌게 반환되면 뭐가 문제가 있다
	bool bUseNoDelay = _bUseNoDelay;
	if(setsockopt(m_Sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&bUseNoDelay, sizeof(bUseNoDelay)) != 0)
	{
		mLOG("Nodelay setting fail %s", _csPort);
		return false;
	}

	//수신 타임아웃 설정
	if(setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&_iTimeOut, sizeof(_iTimeOut)) != 0)
	{
		mLOG("Timeout setting fail %s", _csPort);
		return false;
	}

	//연결 시도
	int iErrorCoded = connect(m_Sock, (sockaddr*)&m_SockInfo, sizeof(m_SockInfo));
	if(iErrorCoded != 0)
	{
		mLOG("Connect error, %d", WSAGetLastError());
		return false;
	}
	mLOG("Connect success");

	//연결 성공했으니 스레드 시작
	begin();

	return true;
}

void cTCPClient::begin()
{
	//상태 변경하고 스레드 시작
	m_iStatus = eTHREAD_STATUS_RUN;
	m_pRecvThread = new std::thread([&]() {recvThread(); });
	m_pSendThread = new std::thread([&]() {sendThread(); });
}

//스레드 정지
void cTCPClient::stop()
{
	//스레드가 멈춰있으면 의미없으니 return
	if(m_iStatus == eTHREAD_STATUS_IDLE
	|| m_iStatus == eTHREAD_STATUS_STOP)
		return;

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//소켓 닫음
	closesocket(m_Sock);

	//자고 있는 스레드들 깨워줌
	m_cvWaiter.notify_all();

	//중단처리 스레드
	if(m_pStoppingThread != nullptr)
	{
		m_pStoppingThread->join();
		KILL(m_pStoppingThread);
	}
	m_pStoppingThread = new std::thread([&]() {stopThread(); });
}

void cTCPClient::stopThread()
{
	//스레드 처리
	m_pSendThread->join();
	m_pRecvThread->join();

	shutdown(m_Sock, SD_SEND);
	closesocket(m_Sock);

	//변수 해제
	KILL(m_pSendThread);
	KILL(m_pRecvThread);

	while(!m_qSendQueue.empty())
	{
		cPacketTCP* pPacket = m_qSendQueue.front();
		m_qSendQueue.pop_front();
		delete pPacket;
	}
	while(!m_qRecvQueue.empty())
	{
		cPacketTCP* pPacket = m_qRecvQueue.front();
		m_qRecvQueue.pop_front();
		delete pPacket;
	}

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;
}