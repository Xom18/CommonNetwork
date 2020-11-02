#include <queue>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include <list>
#include "Debug.h"
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "TCPSocket.h"
#include "TCPSocketServer.h"

void cTCPSocketServer::connectThread()
{
	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(sockaddr_in);

	printf("Begin ConnectThread %d\n", m_iPort);

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		sockaddr_in Client;
		ZeroMemory(&Client, sizeof(Client));

		SOCKET Socket = accept(m_Sock, (sockaddr*)&Client, &ClientAddrLength);
		if(Socket == INVALID_SOCKET)
			continue;

		cTCPSocket* pNewSocket = new cTCPSocket();
		pNewSocket->setSocket(Socket, &Client, ClientAddrLength, &m_iStatus);
		pNewSocket->beginThread();

		//연결된거 IP보려고 넣어둔거, 주석
		char clientIP[256];
		ZeroMemory(clientIP, sizeof(clientIP));
		inet_ntop(AF_INET, &Client.sin_addr, clientIP, sizeof(clientIP));
		printf("Connect success [%s]\n", clientIP);

		//연결대기에 추가
		{
			mAMTX(m_mtxConnectionMutex);
			m_qConnectWait.push(pNewSocket);
		}
	}

	pKILL(pRecvBuffer);

	printf("End recvThread\n");
}

// 처리 스레드
// 패킷 가져오고 전역패킷 보내고 처리
void cTCPSocketServer::operateThread()
{
	printf("Begin SendThread %d\n", m_iPort);

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼

	//삭제 처리용
	std::list<cTCPSocket*> listDeleteWait;
	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		//삭제 처리용
		std::queue<SOCKET> qRemoveInMap;

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
					qRemoveInMap.push(iter->first);
					continue;
				}

				//패킷 수신 처리
				{
					mAMTX(m_mtxRecvMutex);
					lpTCPSocket->getRecvQueue(&m_qRecvQueue);
				}

				//전역송신 패킷 있으면 그거 보내주기
				if(bIsNotHaveSend == false)
				{
					std::deque<cPacketTCP*> qSendQueue = qGlobalSendQueue;
					lpTCPSocket->pushSend(&qSendQueue);
				}
			}
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
				qRemoveInMap.pop();

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
	}

	pKILL(pSendBuffer);
	printf("End sendThread\n");
}

//스레드 시작
void cTCPSocketServer::begin(int _iPort, int _iTimeOut, bool _bUseNoDelay)
{
	if(m_pConnectThread != nullptr
	|| m_pOperateThread != nullptr)
	{
		printf("이미 구동중인 스레드가 있다\n");
		return;
	}

	WSADATA wsaData;							//윈속 데이터
	WORD wVersion = MAKEWORD(1, 0);				//버전
	int iWSOK = WSAStartup(wVersion, &wsaData);	//소켓 시작
	if(iWSOK != 0)
	{
		printf("소켓 시작 에러\n");
		ExitProcess(EXIT_FAILURE);
	}

	m_iPort = _iPort;									//포트
	m_Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);	//소켓 생성
	m_SockInfo.sin_addr.S_un.S_addr = ADDR_ANY;		//전체 대상

	m_SockInfo.sin_family = AF_INET;					//TCP 사용
	m_SockInfo.sin_port = htons(m_iPort);				//포트

	//노딜레이 옵션, 0아닌게 반환되면 뭐가 문제가 있다
	bool bUseNoDelay = _bUseNoDelay;
	if(setsockopt(m_Sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&bUseNoDelay, sizeof(bUseNoDelay)) != 0)
	{
		printf("TCP 옵션 설정 실패\n");
		return;
	}

	//수신 타임아웃 설정
	if(setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&_iTimeOut, sizeof(_iTimeOut)) != 0)
	{
		printf("TCP 옵션 설정 실패\n");
		return;
	}

	if(bind(m_Sock, (sockaddr*)&m_SockInfo, sizeof(sockaddr_in)) != 0)
	{
		printf("바인딩 에러[%d]\n", WSAGetLastError());
		ExitProcess(EXIT_FAILURE);
	}

	if(listen(m_Sock, SOMAXCONN) != 0)
	{
		printf("리슨 에러\n");
		return;
	}

	//상태 변경하고 스레드 시작
	m_iStatus = eTHREAD_STATUS_RUN;
	m_pConnectThread = new std::thread([&]() {connectThread(); });
	m_pOperateThread = new std::thread([&]() {operateThread(); });
}

//스레드 정지
void cTCPSocketServer::stop()
{
	//스레드가 멈춰있으면 의미없으니 return
	if(m_iStatus == eTHREAD_STATUS_IDLE
	|| m_iStatus == eTHREAD_STATUS_STOP)
		return;

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//accept 소캣 중단
	closesocket(m_Sock);

	//스레드 정지 대기
	m_pOperateThread->join();
	m_pConnectThread->join();

	//변수 해제
	KILL(m_pOperateThread);
	KILL(m_pConnectThread);

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
}