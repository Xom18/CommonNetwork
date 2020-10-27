#include <queue>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "TCPSocket.h"
#include "TCPSocketServer.h"

void cTCPSocketServer::connectThread()
{
	fd_set FdSet;
	struct timeval tv;

	// 어디꺼 볼지 셋팅
	FD_ZERO(&FdSet);
	FD_SET(m_Sock, &FdSet);

	tv.tv_sec = 10;	//10초 타임아웃
	tv.tv_usec = 0;	//밀리초는 없음

	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(sockaddr_in);

	printf("Begin ConnectThread %d\n", m_iPort);

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		sockaddr_in Client;
		ZeroMemory(&Client, sizeof(Client));

		//타임아웃이거나 뭐가 왔거나 에러거나, 데이터왔으면 1, 타임아웃 0, 에러 -1
		int iSelectResult = select((int)m_Sock, &FdSet, NULL, NULL, &tv);
		if(iSelectResult != 1)
			continue;

		cTCPSocket* pNewSocket = new cTCPSocket();
		SOCKET Socket = accept(m_Sock, (sockaddr*)&Client, &ClientAddrLength);
		pNewSocket->setSocket(Socket, &Client, ClientAddrLength);

		//송신자 IP보려고 넣어둔거, 주석
		char clientIP[256];
		ZeroMemory(clientIP, sizeof(clientIP));
		inet_ntop(AF_INET, &Client.sin_addr, clientIP, sizeof(clientIP));
		printf("연결성공[%s]\n", clientIP);

		mAMTX(m_mtxConnectionMutex);

		if(m_qUseEndIndex.empty())//반환된 인덱스가 없으면 제일 뒤쪽에
		{
			pNewSocket->setSerial((int)m_mapTCPSocket.size());
		}
		else
		{//반환된 인덱스가 있으면 그거를 사용
			pNewSocket->setSerial(m_qUseEndIndex.front());
			m_qUseEndIndex.pop();
		}
		m_mapTCPSocket.insert(std::pair<int, cTCPSocket*>(pNewSocket->getSerial(), pNewSocket));

		//스레드 시작
		pNewSocket->beginThread();
	}

	pKILL(pRecvBuffer);

	printf("End recvThread\n");
}

void cTCPSocketServer::sendThread()//송신 스레드
{
	printf("Begin SendThread %d\n", m_iPort);

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//송신할 패킷 버퍼
	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		if(m_qSendQueue.empty())
		{//비어있으면 대기큐꺼 가져온다
			commitSendWaitQueue();

			//대기큐꺼도 비어있으면 10ms동안 대기
			if(m_qSendQueue.empty())
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
			memcpy(pSendBuffer, lpPacket->m_pData, iDataSize);
			//누수없게 바로 해제
			m_qSendQueue.pop();
			delete lpPacket;
		}

		//모두에게 패킷 전송
		if(send(m_Sock, pSendBuffer, iDataSize, 0) == SOCKET_ERROR)
		{
			printf("송신 에러\n");
			continue;
		}

		//if(sendto(m_Sock, pSendBuffer, iDataSize, 0, (sockaddr*)&AddrInfo, sizeof(AddrInfo)) == SOCKET_ERROR)
		//{//송신실패하면 에러
		//	printf("송신 에러\n");
		//	continue;
		//}
	}

	pKILL(pSendBuffer);
	printf("End sendThread\n");
}

//스레드 시작
void cTCPSocketServer::beginThread(int _iPort, int _iTimeOut, bool _bUseNoDelay)
{
	if(m_pConnectThread != nullptr
	|| m_pRecieveThread != nullptr
	|| m_pSendThread != nullptr)
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
	m_pSendThread = new std::thread([&]() {sendThread(); });
}

//스레드 정지
void cTCPSocketServer::stopThread()
{
	//스레드가 멈춰있으면 의미없으니 return
	if(m_iStatus == eTHREAD_STATUS_IDLE)
		return;

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//스레드 정지 대기
	m_pSendThread->join();
	m_pConnectThread->join();

	//변수 해제
	KILL(m_pSendThread);
	KILL(m_pConnectThread);

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;

	//소켓 닫음
	closesocket(m_Sock);

	//	WSACleanup();
}