#include <deque>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include <list>
#include <condition_variable>
#include <set>
#include "Debug.h"
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "TCPSocket.h"
#include "TCPSocketServer.h"

void cTCPSocketServer::connectThread(SOCKET _Socket)
{
	mLOG("Begin connectThread %lld", _Socket);
	int ClientAddrLength = sizeof(unSOCKADDR_IN);

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		unSOCKADDR_IN Client;
		ZeroMemory(&Client, sizeof(Client));

		SOCKET Socket = accept(_Socket, (sockaddr*)&Client, &ClientAddrLength);
		if(Socket == INVALID_SOCKET)
			continue;

		//리스트에 내용이 있을때만 동작
		if(!m_setBWList.empty())
		{
			char csClientIP[_IP_LENGTH];
			if(Client.IPv4.sin_family == AF_INET)
				inet_ntop(AF_INET, &Client.IPv4.sin_addr, csClientIP, sizeof(csClientIP));
			else
				inet_ntop(AF_INET6, &Client.IPv6.sin6_addr, csClientIP, sizeof(csClientIP));

			std::string strIP = csClientIP;

			//찾았는지 여부
			bool bIsFound = false;
			{
				mAMTX(m_mtxBWListMutex);
				bIsFound = m_setBWList.find(strIP) != m_setBWList.end();
			}
			//킥 여부
			bool bIsKick = false;
			
			//블랙리스트일 때 찾았으면 튕겨냄
			//화이트리스트일 때 못찾았으면 튕겨냄
			if(m_bIsBlackList && bIsFound)
				bIsKick = true;
			else if(!m_bIsBlackList && !bIsFound)
				bIsKick = true;

			if(bIsKick)
			{
				closesocket(Socket);
				continue;
			}
		}

		cTCPSocket* pNewSocket = new cTCPSocket();
		pNewSocket->setSocket(Socket, &Client, ClientAddrLength, &m_iStatus);
		pNewSocket->begin();

		//연결대기에 추가
		{
			mAMTX(m_mtxConnectionMutex);
			m_qConnectWait.push_back(pNewSocket);
		}
	}

	mLOG("End connectThread %lld", _Socket);
}

// 처리 스레드
// 패킷 가져오고 전역패킷 보내고 처리
void cTCPSocketServer::operateThread()
{
	mLOG("Begin operateThread\n");

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼

	//삭제 처리용
	std::list<cTCPSocket*> listDeleteWait;
	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		std::chrono::system_clock::time_point StartTime = std::chrono::system_clock::now();
		
		//삭제 처리용
		std::deque<SOCKET> qRemoveInMap;

		//연결 대기중인거 처리
		operateConnectWait();

		//특정 대상 패킷 처리
		if(!m_qTargetSendQueue.empty())
		{
			//패킷 가져오기
			std::deque<cPacketTCP*>	qTargetSendQueue;
			{
				mAMTX(m_mtxTargetSendMutex);
				std::swap(qTargetSendQueue, m_qTargetSendQueue);
			}

			//순서대로 처리해준다
			cPacketTCP* pPacket = qTargetSendQueue.front();
			qTargetSendQueue.pop_front();
			
			std::map<SOCKET, cTCPSocket*>::iterator iter = m_mapTCPSocket.find(pPacket->m_Sock);

			//패킷 대상이 누수방지를 위해 없으면 지워준다
			if(iter == m_mapTCPSocket.end())
				delete pPacket;
			else
				iter->second->pushSend(pPacket);
		}

		bool bIsNotHaveSend = false;
		std::deque<cPacketTCP*>	qGlobalSendQueue;

		//모든 대상 패킷 처리
		if(!m_qGlobalSendQueue.empty())
		{
			mAMTX(m_mtxGlobalSendMutex);
			std::swap(qGlobalSendQueue, m_qGlobalSendQueue);
		}
		else
		{
			bIsNotHaveSend = true;
		}

		std::deque<cPacketTCP*> qRecvQueue;
		//패킷 송수신 처리
		{
			std::map<SOCKET, cTCPSocket*>::iterator iter = m_mapTCPSocket.begin();
			for(; iter != m_mapTCPSocket.end(); ++iter)
			{
				cTCPSocket* lpTCPSocket = iter->second;

				//도는중이 아니다
				if(lpTCPSocket->getSocketStatus() != eTHREAD_STATUS_RUN)
				{
					listDeleteWait.push_back(lpTCPSocket);
					qRemoveInMap.push_back(iter->first);
					continue;
				}

				//패킷 꺼내놓고
				{
					lpTCPSocket->getRecvQueue(&qRecvQueue);
				}

				//전역송신 패킷 있으면 그거 보내주기
				if(bIsNotHaveSend == false)
				{
					std::deque<cPacketTCP*> qSendQueue = qGlobalSendQueue;
					lpTCPSocket->pushSend(&qSendQueue);
				}
			}
		}

		//수신큐로 옮김
		{
			mAMTX(m_mtxRecvMutex);
			m_qRecvQueue.insert(m_qRecvQueue.end(), qRecvQueue.begin(), qRecvQueue.end());
		}

		//송신한 전역 패킷 지우기
		while(!qGlobalSendQueue.empty())
		{
			cPacketTCP* qSendQueue = qGlobalSendQueue.front();
			qGlobalSendQueue.pop_front();
			delete qSendQueue;
		}

		//삭제하려는거 map에서 제거
		if(!qRemoveInMap.empty())
		{
			mAMTX(m_mtxSocketMutex);
			while(!qRemoveInMap.empty())
			{
				SOCKET Sock = qRemoveInMap.front();
				qRemoveInMap.pop_front();

				m_mapTCPSocket.erase(Sock);
			}
		}

		//삭제목록에 있는거 처리
		//이번타임이 아니여도 된다, 다음 루프일수도 있고 다다음 루프일수도 있고
		{
			std::list<cTCPSocket*>::iterator iter = listDeleteWait.begin();
			while(iter != listDeleteWait.end())
			{
				cTCPSocket* pSocket = *iter;

				//완전히 섰다
				if(pSocket->getSocketStatus() == eTHREAD_STATUS_IDLE)
				{
					++iter;
					KILL(pSocket);
					continue;
				}
				++iter;
			}
		}

		//설정된 틱만큼 재운다
		std::chrono::duration<double> EndTime = std::chrono::system_clock::now() - StartTime;
		std::chrono::milliseconds msEndTime = std::chrono::duration_cast<std::chrono::milliseconds>(EndTime);

		if(m_iOperateTick < msEndTime.count())
			continue;

		std::chrono::milliseconds msSleepTime(m_iOperateTick - msEndTime.count());
		std::this_thread::sleep_for(msSleepTime);
	}

	pKILL(pSendBuffer);
	mLOG("End operateThread\n");
}

//스레드 시작
bool cTCPSocketServer::begin(const char* _csPort, int _iMode, int _iTick, int _iTimeOut, bool _bUseNoDelay)
{
	if(m_iStatus != eTHREAD_STATUS_IDLE)
	{
		mLOG("Begin error %s", _csPort);
		return false;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(2, 2);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if(iWSOK != 0)
	{
		mLOG("Socket start error %lld", m_Sock);
		return false;
	}

	m_iOperateTick = _iTick;

	//eWINSOCK_USE_IPv4 = 1,	//IPv4 전용
	//eWINSOCK_USE_IPv6 = 2,	//IPv6 전용
	//eWINSOCK_USE_BOTH = 3,	//둘다 사용
	for(int i = 1; i < eWINSOCK_USE_BOTH; ++i)
	{
		if(!(_iMode & i))
			continue;

		//소켓 정보 셋팅
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
		addrIn.ai_socktype = SOCK_STREAM;
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

		//노딜레이 옵션, 0아닌게 반환되면 뭐가 문제가 있다
		bool bUseNoDelay = _bUseNoDelay;
		if(setsockopt(*lpSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&bUseNoDelay, sizeof(bUseNoDelay)) != 0)
		{
			mLOG("Nodelay setting fail %s [%d]", _csPort, WSAGetLastError());
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

		//리슨
		if(listen(*lpSock, SOMAXCONN) != 0)
		{
			mLOG("listen error %s [%d]", _csPort, WSAGetLastError());
			return false;
		}
	}

	m_iStatus = eTHREAD_STATUS_RUN;
	if(_iMode & eWINSOCK_USE_IPv4)
		m_pConnectThread = new std::thread([&]() {connectThread(m_Sock); });
	if(_iMode & eWINSOCK_USE_IPv6)
		m_pConnectIPv6Thread = new std::thread([&]() {connectThread(m_SockIPv6); });
	m_pOperateThread = new std::thread([&]() {operateThread(); });

	return true;
}

//스레드 정지
void cTCPSocketServer::stop()
{
	//스레드가 멈춰있으면 의미없으니 return
	if(m_iStatus == eTHREAD_STATUS_IDLE
	|| m_iStatus == eTHREAD_STATUS_STOP)
		return;

	mLOG("Begin stop %lld", m_Sock);

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//accept 소캣 중단
	closesocket(m_Sock);

	//스레드 정지 대기
	m_pOperateThread->join();
	m_pConnectThread->join();
	m_pConnectIPv6Thread->join();

	//변수 해제
	KILL(m_pOperateThread);
	KILL(m_pConnectThread);
	KILL(m_pConnectIPv6Thread);

	//연결 대기중인거 처리
	operateConnectWait();

	//연결중인거 정지
	std::map<SOCKET, cTCPSocket*>::iterator iter = m_mapTCPSocket.begin();
	for(; iter != m_mapTCPSocket.end(); ++iter)
	{
		iter->second->stop();
	}

	//연결중인거 해제
	//돌고있는 스레드가 없을때까지 계속 돌린다
	iter = m_mapTCPSocket.begin();
	while(!m_mapTCPSocket.empty())
	{
		cTCPSocket* lpSocket = iter->second;

		//완전히 서야 삭제해준다
		//아니면 다시 올려보냄
		if(lpSocket->getSocketStatus() == eTHREAD_STATUS_IDLE)
		{
			KILL(lpSocket);
			std::map<SOCKET, cTCPSocket*>::iterator RemoveIter = iter++;
			m_mapTCPSocket.erase(RemoveIter);
		}
		else
		{
			++iter;
		}

		if(iter == m_mapTCPSocket.end())
			iter = m_mapTCPSocket.begin();
	}

	m_mapTCPSocket.clear();

	while(!m_qGlobalSendQueue.empty())
	{
		cPacketTCP* pPacket = m_qGlobalSendQueue.front();
		m_qGlobalSendQueue.pop_front();
		delete pPacket;
	}
	while(!m_qTargetSendQueue.empty())
	{
		cPacketTCP* pPacket = m_qTargetSendQueue.front();
		m_qTargetSendQueue.pop_front();
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

	mLOG("Success stop %lld", m_Sock);
}