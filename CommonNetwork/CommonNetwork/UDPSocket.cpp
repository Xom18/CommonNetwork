#include <deque>
#include <mutex>
#include <WS2tcpip.h>
#include <chrono>
#include <condition_variable>
#include "Debug.h"
#include "Macro.h"
#include "Define.h"
#include "Packet.h"
#include "UDPSocket.h"

void cUDPSocket::recvThread(SOCKET _Sock)
{
	mLOG("Begin recvThread");

	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(unSOCKADDR_IN);

	while (m_iStatus == eTHREAD_STATUS_RUN)
	{
		unSOCKADDR_IN Client;
		ZeroMemory(pRecvBuffer, _MAX_PACKET_SIZE);	//초기화
		ZeroMemory(&Client, sizeof(Client));

		//데이터 수신
		int iDataLength = recvfrom(_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0, (sockaddr*)&Client, &ClientAddrLength);
		if (iDataLength == SOCKET_ERROR)
			continue;

		//받은 데이터 처리
		cPacketUDP* pPacket = new cPacketUDP();
		pPacket->setData(iDataLength, pRecvBuffer, &Client);

		//큐에 넣는다
		pushRecvQueue(pPacket);
	}

	pKILL(pRecvBuffer);

	mLOG("End recvThread");
}

void cUDPSocket::sendThread(bool _bIsServer)//송신 스레드
{
	mLOG("Begin sendThread");

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼
	while (m_iStatus == eTHREAD_STATUS_RUN)
	{
		//비어있으면 대기
		if (m_qSendQueue.empty())
		{
			std::mutex mtxWaiter;
			std::unique_lock<std::mutex> lkWaiter(mtxWaiter);
			m_cvWaiter.wait(lkWaiter, [&] {
				return !m_qSendQueue.empty() || m_iStatus != eTHREAD_STATUS_RUN;
			});
			lkWaiter.unlock();

			//종료하라고 왔었으면 종료
			if(m_iStatus != eTHREAD_STATUS_RUN)
				break;
		}

		//큐에 있는걸 가져온다
		std::deque<cPacketUDP*>	qSendQueue;
		{
			mLG(m_mtxSendMutex);
			std::swap(qSendQueue, m_qSendQueue);
		}

		while(!qSendQueue.empty())
		{
			ZeroMemory(pSendBuffer, _MAX_PACKET_SIZE);//데이터 초기화
			unSOCKADDR_IN AddrInfo;	//수신자 정보
			int iDataSize = 0;		//데이터 크기

			{//전송을 위해 패킷을 꺼냈다
				cPacketUDP* lpPacket = qSendQueue.front();
				iDataSize = lpPacket->m_iSize;
				memcpy(&AddrInfo, &lpPacket->m_AddrInfo, sizeof(AddrInfo));
				memcpy(pSendBuffer, lpPacket->m_pData, iDataSize);
				//누수없게 바로 해제
				qSendQueue.pop_front();
				delete lpPacket;
			}

			//대상에 맞춰서 맞는 소켓으로 전송
			if(AddrInfo.IPv4.sin_family == AF_INET || !_bIsServer)
			{
				if(sendto(m_Sock, pSendBuffer, iDataSize, 0, (sockaddr*)&AddrInfo, sizeof(AddrInfo)) == SOCKET_ERROR)
				{//송신실패하면 에러
					mLOG("Send Error [%d]", WSAGetLastError());
				}
			}
			else if(AddrInfo.IPv4.sin_family == AF_INET6)
			{
				if(sendto(m_SockIPv6, pSendBuffer, iDataSize, 0, (sockaddr*)&AddrInfo, sizeof(AddrInfo)) == SOCKET_ERROR)
				{//송신실패하면 에러
					mLOG("Send Error [%d]", WSAGetLastError());
				}
			}
		}
	}

	pKILL(pSendBuffer);
	mLOG("End recvThread");
}

//클라이언트 시작
bool cUDPSocket::beginClient(const char* _csIP, const char* _csPort, int _iTimeOut)
{
	mLOG("Begin [Client]%s : %s", _csIP == nullptr ? "null" : _csIP, _csPort);

	if (m_pSendThread != nullptr
	|| m_pRecvThread != nullptr)
	{
		mLOG("Begin error [Client]%s : %s", _csIP == nullptr ? "null" : _csIP , _csPort);
		return false;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(1, 0);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if (iWSOK != 0)
	{
		mLOG("WSAStartup error[Client] %s:%s[% d]", _csIP == nullptr ? "null" : _csIP, _csPort, WSAGetLastError());
		return false;
	}

	//소켓 정보 셋팅
	int iFamily = AF_UNSPEC;
	SOCKET* lpSock = &m_Sock;
	unSOCKADDR_IN* lpSockInfo = &m_SockInfo;

	addrinfo addrIn;
	addrinfo* addrRes;

	memset(&addrIn, 0, sizeof(addrIn));
	addrIn.ai_family = iFamily;
	addrIn.ai_socktype = SOCK_DGRAM;
	addrIn.ai_flags = AI_PASSIVE;

	getaddrinfo(_csIP, _csPort, &addrIn, &addrRes);
	*lpSock = socket(addrRes->ai_family, addrRes->ai_socktype, addrRes->ai_protocol);
	memcpy(lpSockInfo, addrRes->ai_addr, sizeof(unSOCKADDR_IN));

	freeaddrinfo(addrRes);

	if(*lpSock == INVALID_SOCKET)
	{
		mLOG("Socket error %s [%d]", _csPort, WSAGetLastError());
		return false;
	}

	//수신 타임아웃 설정
	if(setsockopt(*lpSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&_iTimeOut, sizeof(_iTimeOut)) != 0)
	{
		mLOG("Timeout setting fail %s [%d]", _csPort, WSAGetLastError());
		return false;
	}

	
	m_iStatus = eTHREAD_STATUS_RUN;

	m_pRecvThread = new std::thread([&]() {recvThread(m_Sock); });
	m_pSendThread = new std::thread([&]() {sendThread(false); });

	return true;
}

//서버 시작
bool cUDPSocket::beginServer(const char* _csPort, int _iMode, int _iTimeOut)
{
	mLOG("Begin [Server]%s", _csPort);

	if(m_pSendThread != nullptr
		|| m_pRecvThread != nullptr)
	{
		mLOG("Begin error[Server]%s", _csPort);
		return false;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(1, 0);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if(iWSOK != 0)
	{
		mLOG("WSAStartup error[Server]%s [%d]", _csPort, WSAGetLastError());
		return false;
	}

	for(int i = 1; i < eWINSOCK_USE_BOTH; ++i)
	{
		if(!(_iMode & i))
			continue;

		int iFamily = AF_INET;
		SOCKET* lpSock = &m_Sock;
		unSOCKADDR_IN* lpSockInfo = &m_SockInfo;
		if(i == eWINSOCK_USE_IPv6)
		{
			iFamily = AF_INET6;
			lpSock = &m_SockIPv6;
			lpSockInfo = &m_SockInfoIPv6;
		}

		addrinfo addrIn;
		addrinfo* addrRes;

		memset(&addrIn, 0, sizeof(addrIn));
		addrIn.ai_family = iFamily;
		addrIn.ai_socktype = SOCK_DGRAM;
		addrIn.ai_flags = AI_PASSIVE;

		getaddrinfo(nullptr, _csPort, &addrIn, &addrRes);
		*lpSock = socket(addrRes->ai_family, addrRes->ai_socktype, addrRes->ai_protocol);
		memcpy(lpSockInfo, addrRes->ai_addr, sizeof(unSOCKADDR_IN));

		freeaddrinfo(addrRes);

		if(*lpSock == INVALID_SOCKET)
		{
			mLOG("Socket error %s [%d]", _csPort, WSAGetLastError());
			return false;
		}

		//수신 타임아웃 설정
		if(setsockopt(*lpSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&_iTimeOut, sizeof(_iTimeOut)) != 0)
		{
			mLOG("Timeout setting fail %s [%d]", _csPort, WSAGetLastError());
			return false;
		}

		//바인드
		if(bind(*lpSock, (sockaddr*)lpSockInfo, sizeof(unSOCKADDR_IN)) != 0)
		{
			mLOG("Bind error %s [%d]", _csPort, WSAGetLastError());
			return false;
		}
	}

	m_iStatus = eTHREAD_STATUS_RUN;

	if(m_Sock != INVALID_SOCKET)
		m_pRecvThread = new std::thread([&]() {recvThread(m_Sock); });
	if(m_SockIPv6 != INVALID_SOCKET)
		m_pRecvThreadIPv6 = new std::thread([&]() {recvThread(m_SockIPv6); });
	m_pSendThread = new std::thread([&]() {sendThread(true); });

	return true;
}

//스레드 정지
void cUDPSocket::stop()
{
	//스레드가 멈춰있으면 의미없으니 return
	if(m_iStatus == eTHREAD_STATUS_IDLE
	|| m_iStatus == eTHREAD_STATUS_STOP)
	{
		return;
	}

	mLOG("Begin stop");

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//소켓 닫음
	closesocket(m_Sock);

	//자고 있는 스레드들 깨워줌
	m_cvWaiter.notify_all();

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
		m_qSendQueue.pop_front();
		delete pPacket;
	}

	while(!m_qRecvQueue.empty())
	{
		cPacketUDP* pPacket = m_qRecvQueue.front();
		m_qRecvQueue.pop_front();
		delete pPacket;
	}

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;
}