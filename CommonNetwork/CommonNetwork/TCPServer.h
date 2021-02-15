#pragma once

#include "TCPSClient.h"
//TCP 통신 서버 처리 하는곳
//TCP는 서버와 클라이언트가 꽤 달라서 코드 분리했음

enum
{
	eTCP_IPv4 = 0,
	eTCP_IPv6,
	eTCP_TypeCount,
};

class cTCPServer
{
private:
	std::mutex m_mtxRecvMutex;					//수신 뮤텍스
	std::mutex m_mtxClientMutex;				//연결 뮤텍스
	std::mutex m_mtxBWListMutex;				//블랙/화이트 리스트 뮤텍스

	std::deque<cPacketTCP*>	m_qRecvQueue;		//수신 큐

	std::thread* m_pWorkThread;					//처리 스레드(송신, 수신)

	int		m_iStatus;							//상태 -1정지요청, 0정지, 1돌아가는중
	int		m_iOperateTick;						//처리 간격

	int		m_iMaxConnectSocket;				//최대 연결 가능한 소켓 수
	int		m_iConnectedSocketCount;			//이미 연결 된 소켓 수
	std::vector<cTCPSClient*> m_vecClient;		//연결 되 있는 클라이언트

	int		m_iLastConnectIndex;				//마지막 연결 인덱스
	std::deque<int> m_qDisconnectedIndex;		//반환된 마지막 연결 인덱스

	struct stSocket
	{
		SOCKET	m_Sock;							//소켓
		unSOCKADDR_IN m_SockInfo;				//소켓 정보
		std::thread* m_pAcceptThread;			//연결 스레드
	}m_Socket[eTCP_TypeCount];

	HANDLE	m_hCompletionPort;					//iocp처리포트

	bool	m_bIsBlackList;						//블랙 리스트가 아니면 화이트 리스트
	std::set<std::string> m_setBWList;			//블랙, 화이트 리스트

	int m_iRunningMode;							//동작 모드

public:

	/// <summary>
	/// 생성자
	/// </summary>
	cTCPServer()
	{
		m_iRunningMode = 0;
		m_pWorkThread = nullptr;	//수신 스레드
		m_iStatus = eTHREAD_STATUS_IDLE;//상태
		m_bIsBlackList = true;		//블랙리스트
		m_iMaxConnectSocket = _MAX_TCP_CLIENT_COUNT;	//최대 연결 가능한 소켓 수
		m_iConnectedSocketCount = 0;					//현재 연결되어있는 소켓 수

		for (int i = 0; i < eTCP_TypeCount; ++i)
		{
			m_Socket[i].m_Sock = INVALID_SOCKET;
			m_Socket[i].m_pAcceptThread = nullptr;	//연결 대기 스레드
			ZeroMemory(&m_Socket[i].m_SockInfo, sizeof(m_Socket[i].m_SockInfo));
		}
	};

	/// <summary>
	/// 소멸자
	/// </summary>
	~cTCPServer()
	{
		stop();
	}

private:
	/// <summary>
	/// 연결 수립 스레드
	/// </summary>
	/// <param name="_Socket">대상 소켓</param>
	void acceptThread(SOCKET _Socket);

	/// <summary>
	/// 처리 스레드
	/// 패킷 가져오고 전역패킷 보내고 처리
	/// </summary>
	void workThread();

	/// <summary>
	/// 연결 즉시 종료
	/// </summary>
	/// <param name="_Socket">연결 종료 할 소켓 인덱스</param>
	void disconnectNow(SOCKET _Socket)
	{
		shutdown(_Socket, SD_SEND);
		closesocket(_Socket);
	}

protected:

	/// <summary>
	/// 패킷 유효성 검사
	/// 본격적인 게임 처리로 가기전에 처리하는 부분이라 패킷 크기 초과나
	/// 정상적인 절차중인지(로그인 전인대 로그인 이외의 패킷 보내거나 그런거)만 확인
	/// </summary>
	/// <param name="_lpPacket">패킷</param>
	/// <returns>정상패킷 true 아니면 false</returns>
	virtual bool packetValidityCheck(stPacketBase* _lpPacket)
	{
		return true;
	}

public:

	/// <summary>
	/// 서버 시작
	/// </summary>
	/// <param name="_iPort">포트(기본 58326)</param>
	/// <param name="_bUseNoDelay">IPv4 IPv6 선택eWINSOCK_USE_IPv4 / eWINSOCK_USE_IPv6 / eWINSOCK_USE_BOTH</param>
	/// <param name="_iTick">처리 틱 간격(ms)</param>
	/// <param name="_iTimeOut">타임아웃 옵션</param>
	/// <param name="_bUseNoDelay">노딜레이 옵션</param>
	bool begin(const char* _csPort = _DEFAULT_PORT, int _iMode = eWINSOCK_USE_BOTH, int _iTick = _DEFAULT_TICK, int _iTimeOut = _DEFAULT_TIME_OUT, bool _bUseNoDelay = false);

	/// <summary>
	/// 스레드 정지
	/// </summary>
	void stop();
	
	/// <summary>
	/// 신규 클라이언트 추가, 포인터 반환
	/// </summary>
	/// <returns>클라이언트 포인터</returns>
	cTCPSClient* addNewClient();

	/// <summary>
	/// 해당 인덱스의 클라이언트 삭제
	/// </summary>
	void deleteClient(int _iIndex)
	{
		if (_iIndex >= m_iMaxConnectSocket)
			return;

		mLG(m_mtxClientMutex);
		--m_iConnectedSocketCount;
		m_vecClient[_iIndex]->setNotUse();
		m_qDisconnectedIndex.push_back(_iIndex);
	}

	/// <summary>
	/// 클라이언트 받아오기
	/// </summary>
	/// <param name="_iIndex">클라이언트 인덱스</param>
	/// <returns>클라이언트 포인터</returns>
	cTCPSClient* getClient(int _iIndex)
	{
		if (_iIndex >= m_iMaxConnectSocket)
			return nullptr;

		if (m_vecClient[_iIndex] == nullptr)
			return nullptr;
		if (m_vecClient[_iIndex]->getSocket() == INVALID_SOCKET
		|| !m_vecClient[_iIndex]->isUse())
			return nullptr;
		return m_vecClient[_iIndex];
	}

	/// <summary>
	/// 최대 연결 가능한 클라이언트 수 설정
	/// </summary>
	void setMaxClientCount(int _iCount)
	{
		m_iMaxConnectSocket = _iCount;
	}

	/// <summary>
	/// 소켓 상태 받아오는 함수, -1 정지요청, 0 정지, 1 돌아가는중
	/// </summary>
	/// <returns></returns>
	inline int getSocketStatus()
	{
		return m_iStatus;
	}

	/// <summary>
	/// 소켓 정보 받아오는거
	/// </summary>
	/// <returns>m_SockInfo</returns>
	inline unSOCKADDR_IN getSockinfo(int _iSockType)
	{
		return m_Socket[_iSockType].m_SockInfo;
	}

	/// <summary>
	/// 패킷 송신
	/// </summary>
	/// <param name="_Socket">대상</param>
	/// <param name="_iSize">데이터 크기</param>
	/// <param name="_lpData">데이터</param>
	inline void sendPacket(int _iIndex, int _iSize, char* _lpData)
	{
		mLG(m_mtxClientMutex);
		cTCPSClient* lpClient = getClient(_iIndex);
		if (!lpClient)
			return;

		lpClient->addSendPacket(_iSize, _lpData);
	}

	/// <summary>
	/// 수신된 패킷 큐에 있는걸 받아오는거
	/// </summary>
	/// <param name="_lpQueue">복사 뜰 비어있는 queue 변수</param>
	/// <param name="_bFlush">수신 큐 초기화 여부</param>
	inline bool getRecvQueue(std::deque<cPacketTCP*>* _lpQueue, bool _bFlush = true)
	{
		if(m_qRecvQueue.empty())
			return false;

		mLG(m_mtxRecvMutex);
		_lpQueue->insert(_lpQueue->end(), m_qRecvQueue.begin(), m_qRecvQueue.end());

		if(_bFlush)
			m_qRecvQueue.clear();
		return true;
	}

	/// <summary>
	/// 마찬가지로 수신된 패킷 큐에 있는걸 받아오는거
	/// getRecvQueue와 다르게 인자로 들어온 변수에 덮어쓰는거
	/// </summary>
	/// <param name="_lpQueue">복사 뜰 비어있는 queue 변수</param>
	inline bool swapRecvQueue(std::deque<cPacketTCP*>* _lpQueue)
	{
		if(m_qRecvQueue.empty())
			return false;

		mLG(m_mtxRecvMutex);
		std::swap(m_qRecvQueue, *_lpQueue);
		return true;
	}

	/// <summary>
	/// 블랙리스트로 설정
	/// </summary>
	inline void setBlackList()
	{
		m_bIsBlackList = true;
	}

	/// <summary>
	/// 블랙리스트로 설정
	/// </summary>
	/// <param name="_bIsBlackList">블랙리스트인지, false면 화이트 리스트(기본 true)</param>
	inline void setWhiteList()
	{
		m_bIsBlackList = false;
	}

	/// <summary>
	/// 블랙/화이트 리스트에 대상 추가
	/// </summary>
	/// <param name="_lpIP">대상 IP</param>
	inline void addBWList(std::string* _lpIP)
	{
		mLG(m_mtxBWListMutex);
		m_setBWList.insert(*_lpIP);
	}

	/// <summary>
	/// 블랙/화이트 리스트에 대상 제거
	/// </summary>
	/// <param name="_lpIP">대상 IP</param>
	inline void removeBWList(std::string* _lpIP)
	{
		mLG(m_mtxBWListMutex);
		m_setBWList.erase(*_lpIP);
	}
};