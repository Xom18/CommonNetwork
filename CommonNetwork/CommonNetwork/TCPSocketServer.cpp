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
#include "AutoMutex.h"
#include "Packet.h"
#include "TCPSocketServer.h"

bool cTCPClient::recvPacket()
{
	DWORD	dwRecv;
	DWORD	Flags = 0;
	if (WSARecv(m_Sock, &m_RecvOL.buf, 1, &dwRecv, &Flags, &m_RecvOL.OL, NULL) == SOCKET_ERROR)
	{
		DWORD dwError = WSAGetLastError();

		if (dwError != WSA_IO_PENDING && dwError != ERROR_SUCCESS)
			return false;
	}

	return true;
}

//전송 대기중인 다음 패킷 가져오는거
bool cTCPClient::pullNextPacket()
{
	cPacketTCP* pPacket = nullptr;
	
	{
		mAMTX(m_mtxSendMutex);
		if (m_qSendQueue.empty())
			return false;

		pPacket = m_qSendQueue.front();
		m_qSendQueue.pop_front();
	}
	m_SendOL.sendlen = pPacket->m_iSize;
	memcpy(m_SendOL.Buffer, pPacket->m_pData, pPacket->m_iSize);
	delete pPacket;

	return true;
}
void cTCPClient::addSendPacket(int _iSize, const char* _lpData)
{
	//사용중이지 않음
	if (!m_bIsUse)
		return;

	//현재 송신중이면 큐에 넣어놓고
	if (m_bIsSending)
	{
		cPacketTCP* pNewPacket = new cPacketTCP();
		pNewPacket->setData(_iSize, _lpData);

		mAMTX(m_mtxSendMutex);
		m_qSendQueue.push_back(pNewPacket);
	}
	else
	{//송신중이 아니면 바로 전송시도
		memcpy(m_SendOL.Buffer, _lpData, _iSize);
		m_SendOL.sendlen = _iSize;
		sendPacket();
	}
}

bool cTCPClient::sendPacket()
{
	//사용중이지 않음
	if (!m_bIsUse)
		return false;

	//전송상태 true로 전환
	m_bIsSending = true;

	DWORD	dwSend;

	if (WSASend(m_Sock, &m_SendOL.buf, 1, &dwSend, 0, &m_SendOL.OL, NULL) == SOCKET_ERROR)
	{
		DWORD dwError = WSAGetLastError();

		if (dwError != WSA_IO_PENDING && dwError != ERROR_SUCCESS)
			return false;
	}

	return true;
}

void cTCPSocketServer::acceptThread(SOCKET _Socket)
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

		//새 클라이언트 생성
		cTCPClient* lpClient = addNewClient();
		if (lpClient == nullptr)
		{
			disconnectNow(Socket);
			continue;
		}
		lpClient->setSocket(Socket);

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
void cTCPSocketServer::workThread()
{
	mLOG("Begin operateThread\n");

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼

	//삭제 처리용
	//std::list<cTCPSocket*> listDeleteWait;

	LP_IO_DATA	lpOV;
	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
//		cTCPClient* lpClient = nullptr;
		int iIndex = -1;

		size_t szRecvSize = 0;
		BOOL returnValue = GetQueuedCompletionStatus(m_hCompletionPort, (LPDWORD)&szRecvSize, (PULONG_PTR)&iIndex, (LPOVERLAPPED*)&lpOV, _DEFAULT_TIME_OUT);

		//이쪽으로 오면 타임아웃인 경우?
		if (iIndex == -1)
			continue;

		cTCPClient* lpClient = getClient(iIndex);
		if (lpClient == nullptr)
			continue;

		//송신처리
		if (lpOV->IOState == 1)
		{
			//다음 패킷이 있으면 그거 전송 없으면 송신상태 종료
			if (lpClient->pullNextPacket())
				lpClient->sendPacket();
			else
				lpClient->setSendFinish();

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

					cPacketTCP* pRecvPacket = new cPacketTCP();
					pRecvPacket->m_iIndex = lpClient->getIndex();
					pRecvPacket->setData(lpPacketInfo->m_iSize, lpData->Buffer + szSplitSize, lpClient->getIndex());
					szSplitSize += lpPacketInfo->m_iSize;
					qSplitPacket.push_back(pRecvPacket);

					//다읽었다 스탑
					if (szSplitSize >= szRecvSize)
						break;
				}

				mAMTX(m_mtxRecvMutex);
				m_qRecvQueue.insert(m_qRecvQueue.end(), qSplitPacket.begin(), qSplitPacket.end());
			}

			//다시 수신상태로 바꿔줌
			if (!lpClient->recvPacket())
			{
				deleteClient(lpClient->getIndex());
				shutdown(lpClient->getSocket(), SD_SEND);
				closesocket(lpClient->getSocket());
				continue;
			}
		}
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
void cTCPSocketServer::stop()
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
	for (int i = 0; i < m_vecClient.size(); ++i)
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