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
	fd_set FdSet;
	struct timeval tv;

	// 어디꺼 볼지 셋팅
	FD_ZERO(&FdSet);
	FD_SET(m_Sock, &FdSet);

	tv.tv_sec = 10;	//10초 타임아웃
	tv.tv_usec = 0;	//밀리초는 없음

	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//데이터 버퍼
	int ClientAddrLength = sizeof(sockaddr_in);

	while(m_iStatus == eTHREAD_STATUS_RUN)
	{
		sockaddr_in Client;
		ZeroMemory(pRecvBuffer, _MAX_PACKET_SIZE);	//초기화
		ZeroMemory(&Client, sizeof(Client));


		//타임아웃이거나 데이터가 왔거나 에러거나, 데이터왔으면 1, 타임아웃 0, 에러 -1
		int iSelectResult = select((int)m_Sock, &FdSet, NULL, NULL, &tv);
		if(iSelectResult != 1)
			continue;

		//데이터 수신
		int iDataLength = recvfrom((int)m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0, (sockaddr*)&Client, &ClientAddrLength);
		if(iDataLength == SOCKET_ERROR)
		{
			printf("수신에러\n");
			continue;
		}

		//받은 데이터 처리
		cPacket* pPacket = new cPacket();
		pPacket->setData(&Client, iDataLength, pRecvBuffer);

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
	printf("TCP 연결 [%s : %d]\n", _csIP, _iPort);

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
	if(m_iStatus == eTHREAD_STATUS_IDLE)
		return;

	//스레드 멈추게 변수 바꿔줌
	m_iStatus = eTHREAD_STATUS_STOP;

	//스레드 정지 대기
	m_pSendThread->join();
	m_pRecvThread->join();

	//변수 해제
	KILL(m_pSendThread);
	KILL(m_pRecvThread);

	//상태 재설정
	m_iStatus = eTHREAD_STATUS_IDLE;

	//소켓 닫음
	closesocket(m_Sock);
}