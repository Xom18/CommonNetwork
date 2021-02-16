#include <deque>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include <list>
#include <condition_variable>
#include <set>
#include <vector>
#include "Debug.h"
#include "Macro.h"
#include "Define.h"
#include "Packet.h"
#include "TCPServer.h"

cTCPSClient* cTCPServer::addNewClient()
{
	//정원초과
	if (m_iConnectedSocketCount >= m_iMaxConnectSocket)
		return nullptr;

	mLG(m_mtxClientMutex);

	//사용자가 나가서 반환된 인덱스 재사용
	int iIndex = m_iLastConnectIndex;
	if (!m_qDisconnectedIndex.empty())
	{
		iIndex = m_qDisconnectedIndex.front();
		m_qDisconnectedIndex.pop_front();
	}
	else
	{
		++m_iLastConnectIndex;
	}

	++m_iConnectedSocketCount;

	//해당 인덱스에 할당되있는게 없으면 할당
	if (m_vecClient[iIndex] == nullptr)
	{
		cTCPSClient* pNewClient = new cTCPSClient();
		pNewClient->setIndex(iIndex);
		m_vecClient[iIndex] = pNewClient;
	}

	if (m_vecClient[iIndex]->isUse())
		return nullptr;

	m_vecClient[iIndex]->setUse();
	return m_vecClient[iIndex];
}

void cTCPServer::acceptThread(SOCKET _Socket)
{
	mLOG("Begin connectThread");
	int ClientAddrLength = sizeof(unSOCKADDR_IN);
	int iMaxBufferSize = _MAX_PACKET_SIZE;

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		unSOCKADDR_IN Client;
		ZeroMemory(&Client, sizeof(Client));

		SOCKET Socket = accept(_Socket, (sockaddr*)&Client, &ClientAddrLength);
		if(Socket == INVALID_SOCKET)
			continue;

		//만약 게임서버같은거 만든다고 치면 비정상적인 접근 방지를 위해 화이트 리스트 사용 권장
		//로그인서버 인증->게임서버 화이트리스트 추가 하는 식으로

		//블랙리스트는 리스트에 내용이 있을때만 동작, 화이트 리스트는 없어도 동작
		if(!m_setBWList.empty() || !m_bIsBlackList)
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
				mLG(m_mtxBWListMutex);
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
				disconnectNow(Socket);
				continue;
			}
		}
		
		//버퍼 크기 잡아줌
		if (SOCKET_ERROR == setsockopt(Socket, SOL_SOCKET, SO_SNDBUF, (const char*)&iMaxBufferSize, sizeof(iMaxBufferSize)))
		{
			disconnectNow(Socket);
			continue;
		}

		if (SOCKET_ERROR == setsockopt(Socket, SOL_SOCKET, SO_RCVBUF, (const char*)&iMaxBufferSize, sizeof(iMaxBufferSize)))
		{
			disconnectNow(Socket);
			continue;
		}

		LINGER	lingerStruct;

		lingerStruct.l_onoff = 1;
		lingerStruct.l_linger = 0;

		if (SOCKET_ERROR == setsockopt(Socket, SOL_SOCKET, SO_LINGER, (char*)&lingerStruct, sizeof(lingerStruct)))
		{
			disconnectNow(Socket);
			continue;
		}

		//클라이언트 할당
		cTCPSClient* lpClient = addNewClient();
		if (lpClient == nullptr)
		{
			disconnectNow(Socket);
			continue;
		}

		//소켓과 소켓정보 설정
		lpClient->setSocket(Socket);
		lpClient->setInfo(Client);

		//IOCP 잡아줌
		if (!CreateIoCompletionPort((HANDLE)Socket, m_hCompletionPort, (ULONG_PTR)lpClient->getIndex(), 0))
		{
			deleteClient(lpClient->getIndex());
			disconnectNow(Socket);
			continue;
		}

		//패킷 수신 상태로 못들어가면 뭔가 이상한거
		if (!lpClient->recvPacket())
		{
			deleteClient(lpClient->getIndex());
			disconnectNow(Socket);
			continue;
		}
	}

	mLOG("End connectThread");
}

// 수신 스레드
// 패킷 가져오고 전역패킷 보내고 처리
void cTCPServer::workThread()
{
	mLOG("Begin operateThread\n");

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼

	//삭제 처리용
	//std::list<cTCPSocket*> listDeleteWait;

	LP_IO_DATA	lpOV;
	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		int iIndex = -1;

		size_t szRecvSize = 0;
		BOOL returnValue = GetQueuedCompletionStatus(m_hCompletionPort, (LPDWORD)&szRecvSize, (PULONG_PTR)&iIndex, (LPOVERLAPPED*)&lpOV, _DEFAULT_TIME_OUT);

		//이쪽으로 오면 타임아웃인 경우?
		if (iIndex == -1)
			continue;

		//여기서 연결 끊는걸 관리하기 때문에 mutex 안잡고 그냥씀
		cTCPSClient* lpClient = getClient(iIndex);
		if (lpClient == nullptr)
			continue;

		//송신처리
		if (lpOV->IOState == 1)
		{
			//다음 패킷이 있으면 그거 전송 없으면 송신상태 종료
			if (lpClient->pullNextPacket())
				lpClient->sendPacket();

			continue;
		}

		//수신처리
		if (lpOV->IOState == 0)
		{
			//연결종료되는 경우
			if (returnValue == FALSE && szRecvSize == 0)
			{
				disconnectNow(lpClient->getSocket());
				deleteClient(lpClient->getIndex());
				continue;
			}

			//연결종료되는 경우
			if (szRecvSize == 0)
			{
				disconnectNow(lpClient->getSocket());
				deleteClient(lpClient->getIndex());
				continue;
			}

			//수신큐로 옮김
			{
				IO_DATA* lpData = lpClient->getRecvOL();

				//패킷이 뭉쳐서 온 경우가 있어서 자름
				std::deque<cPacketTCP*> qSplitPacket;
				size_t szSplitSize = 0;

				//한번에 일정개수 이상 못보내게 처리
				//만약 정말 많은패킷이 오고가는 게임이라면 스레드를 분리하거나 패킷을 묶어서 보내게 처리권장
				for (int i = 0; i < _MAX_SEND_PACKET_ONCE; ++i)
				{
					stPacketBase* lpPacketInfo = reinterpret_cast<stPacketBase*>(lpData->Buffer + szSplitSize);

					//이대로 읽으면 패킷 크기를 넘는다
					if (szSplitSize + lpPacketInfo->m_iSize > szRecvSize)
						break;

					//패킷 유효성 검사
					if (!packetValidityCheck(lpPacketInfo))
						break;

					cPacketTCP* pRecvPacket = new cPacketTCP();
					pRecvPacket->m_iIndex = lpClient->getIndex();
					pRecvPacket->setData(lpPacketInfo->m_iSize, lpData->Buffer + szSplitSize, lpClient->getIndex());
					szSplitSize += lpPacketInfo->m_iSize;
					qSplitPacket.push_back(pRecvPacket);

					//다읽었다 스탑
					if (szSplitSize >= szRecvSize)
						break;
				}

				//만약 패킷이 처리 가능한 수 보다 많이 몰리면 이쪽에 서버에서 갖고있을 수 있는 최대 패킷 수 같은걸 제한하자
				mLG(m_mtxRecvMutex);
				m_qRecvQueue.insert(m_qRecvQueue.end(), qSplitPacket.begin(), qSplitPacket.end());
			}

			//다시 수신상태로 바꿔줌
			if (!lpClient->recvPacket())
			{
				deleteClient(lpClient->getIndex());
				shutdown(lpClient->getSocket(), SD_BOTH);
				closesocket(lpClient->getSocket());
				continue;
			}
		}
	}

	pKILL(pSendBuffer);
	mLOG("End operateThread\n");
}

//스레드 시작
bool cTCPServer::begin(const char* _csPort, int _iMode, int _iTick, int _iTimeOut, bool _bUseNoDelay)
{
	if(m_iStatus != eTHREAD_STATUS_IDLE)
	{
		mLOG("Begin error %s", _csPort);
		return false;
	}

	m_iRunningMode = _iMode;

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(2, 2);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if(iWSOK != 0)
	{
		mLOG("Socket start error %d", WSAGetLastError());
		return false;
	}

	m_iOperateTick = _iTick;

	//eWINSOCK_USE_IPv4 = 1,	//IPv4 전용
	//eWINSOCK_USE_IPv6 = 2,	//IPv6 전용
	//eWINSOCK_USE_BOTH = 3,	//둘다 사용
	for(int i = 1; i < eWINSOCK_USE_BOTH; ++i)
	{
		if(!(m_iRunningMode & i))
			continue;

		//소켓 정보 셋팅
		int iFamily = AF_INET;
		SOCKET* lpSock = &m_Socket[i - 1].m_Sock;
		unSOCKADDR_IN* lpSockInfo = &m_Socket[i - 1].m_SockInfo;
		if(i == eWINSOCK_USE_IPv6)
			iFamily = AF_INET6;

		addrinfo addrIn;
		addrinfo* addrRes;

		memset(&addrIn, 0, sizeof(addrIn));
		addrIn.ai_family = iFamily;
		addrIn.ai_socktype = SOCK_STREAM;
		addrIn.ai_flags = AI_PASSIVE;

		getaddrinfo(nullptr, _csPort, &addrIn, &addrRes);

		*lpSock = WSASocket(iFamily, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
//		*lpSock = socket(addrRes->ai_family, addrRes->ai_socktype, addrRes->ai_protocol);

		memcpy(lpSockInfo, addrRes->ai_addr, sizeof(unSOCKADDR_IN));

		freeaddrinfo(addrRes);

		//소켓 생성 실패
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

	m_vecClient.resize(m_iMaxConnectSocket);

	m_hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	m_iStatus = eTHREAD_STATUS_RUN;
	if(m_iRunningMode & eWINSOCK_USE_IPv4)
		m_Socket[eTCP_IPv4].m_pAcceptThread = new std::thread([&]() {acceptThread(m_Socket[eTCP_IPv4].m_Sock); });
	if(m_iRunningMode & eWINSOCK_USE_IPv6)
		m_Socket[eTCP_IPv6].m_pAcceptThread = new std::thread([&]() {acceptThread(m_Socket[eTCP_IPv6].m_Sock); });
	m_pWorkThread = new std::thread([&]() {workThread(); });

	return true;
}

//스레드 정지
void cTCPServer::stop()
{
	//스레드가 멈춰있으면 의미없으니 return
	if(m_iStatus == eTHREAD_STATUS_IDLE
	|| m_iStatus == eTHREAD_STATUS_STOP)
		return;

	mLOG("Begin stop");

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//스레드 정지 대기
	m_pWorkThread->join();

	//변수 해제
	KILL(m_pWorkThread);

	//클라이언트 해제
	for (size_t i = 0; i < m_vecClient.size(); ++i)
	{
		if (m_vecClient[i] == nullptr)
			continue;

		if (m_vecClient[i]->isUse())
			disconnectNow(m_vecClient[i]->getSocket());

		delete m_vecClient[i];
		m_vecClient[i] = nullptr;
	}

	//소켓 중단
	for (int i = 1; i < eWINSOCK_USE_BOTH; ++i)
	{
		if (!(m_iRunningMode & i))
			continue;

		stSocket* lpSocket = &m_Socket[i - 1];

		disconnectNow(lpSocket->m_Sock);
		lpSocket->m_pAcceptThread->join();
		KILL(lpSocket->m_pAcceptThread);
	}

	while(!m_qRecvQueue.empty())
	{
		cPacketTCP* pPacket = m_qRecvQueue.front();
		m_qRecvQueue.pop_front();
		delete pPacket;
	}

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;

	mLOG("Success stop");
}