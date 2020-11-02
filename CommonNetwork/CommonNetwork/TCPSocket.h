#pragma once
//TDP 통신 클라 처리 하는곳
//TDP는 서버와 클라이언트가 꽤 달라서 코드 분리했음

//사용법
//cUDPSocket 변수 선언
//beginThread 호출
//copyRecvQueue나 popRecvQueue를 통해서 패킷 꺼내가지고 처리한 뒤
//처리한 패킷 반드시 delete처리 필요, 수신받은 패킷은 다 동적할당 되있는거다

//연결 처리하는곳
//송신큐와 송신 대기큐를 둠으로써 송신스레드에서는 송신할 때 매번 뮤텍스 호출을 안하고 패킷 빌때만 뮤텍스로 송신대기큐에서 당겨올때만 하면 된다
//송신큐만 대기큐가 있는이유는 수신은 copyRecvQueue로 외부 큐에서 가져가고 있기 때문에 
//pushSend호출->송신대기큐(m_qSendWaitQueue)->송신큐(m_qSendQueue)->송신
//수신큐(m_qRecvQueue)->copyRecvQueue호출->사용하는 프로그램에 맞게 꺼내쓰기, 반드시 꺼내쓰고 변수제거
class cTCPSocket
{
private:
	int*	m_lpMasterStatus;					//서버에서 지정하는 상태
	int		m_iStatus;							//상태 -1정지요청, 0정지, 1돌아가는중
	SOCKET	m_Sock;								//소켓
	sockaddr_in m_SockInfo;						//소켓 정보

	std::mutex m_mtxSendMutex;					//송신 뮤텍스
	std::mutex m_mtxRecvMutex;					//수신 뮤텍스

	std::deque<cPacketTCP*>	m_qSendQueue;		//송신 큐
	std::deque<cPacketTCP*>	m_qRecvQueue;		//수신 큐
	std::thread* m_pRecvThread;					//수신 스레드
	std::thread* m_pSendThread;					//송신 스레드
	std::thread* m_pStoppingThread;				//중단 스레드

public:
	cTCPSocket()//생성자
	{
		m_pRecvThread = nullptr;	//연결 대기 스레드
		m_pSendThread = nullptr;	//연결 대기 스레드
		m_pStoppingThread = nullptr;
		m_lpMasterStatus = nullptr;
		m_iStatus = eTHREAD_STATUS_IDLE;//상태
		m_Sock = INVALID_SOCKET;
		ZeroMemory(&m_SockInfo, sizeof(m_SockInfo));
	};

	~cTCPSocket()//소멸자
	{
		stop();
		if(m_pStoppingThread != nullptr)
			m_pStoppingThread->join();
		KILL(m_pStoppingThread);
	}

private:

	/// <summary>
	/// 중단 마무리 스레드
	/// </summary>
	void stoppingThread();

	/// <summary>
	/// 수신 스레드
	/// </summary>
	void recvThread();

	/// <summary>
	/// 송신 스레드
	/// 전역송신이 대상인 패킷만 여기서 처리
	/// </summary>
	void sendThread();

	/// <summary>
	/// 수신받을걸 수신큐에 넣는 함수
	/// </summary>
	/// <param name="_lpPacket">수신받은 패킷</param>
	inline void pushRecvQueue(cPacketTCP* _lpPacket)
	{
		mAMTX(m_mtxRecvMutex);
		m_qRecvQueue.push_back(_lpPacket);
	}

public:
	/// <summary>
	/// 소켓 받아오기
	/// </summary>
	/// <returns>m_Sock</returns>
	inline SOCKET getSocket()
	{
		return m_Sock;
	}

	/// <summary>
	/// 이쪽이 서버일경우 accept된걸 셋팅하는쪽
	/// </summary>
	/// <param name="_Sock">연결요청자의 소켓</param>
	/// <param name="_Addr">연결요청자의 Addr</param>
	/// <param name="_iAddrLength">Addr 크기</param>
	void setSocket(SOCKET _Sock, sockaddr_in* _Addr, int _iAddrLength, int* _lpMasterStatus)
	{
		m_Sock = _Sock;
		memcpy(&m_SockInfo, _Addr, _iAddrLength);
	}

	/// <summary>
	/// 클라이언트의 경우에 이쪽을 통해서 연결요청을 넣는다
	/// </summary>
	/// <param name="_csIP">IP</param>
	/// <param name="_iPort">포트(기본 58326)</param>
	bool tryConnectServer(char* _csIP, int _iPort = _DEFAULT_PORT, int _iTimeOut = _DEFAULT_TIME_OUT, bool _bUseNoDelay = false);

	/// <summary>
	/// 소켓 동작 시작, 연결 직후 호출
	/// </summary>
	void beginThread();

	/// <summary>
	/// 스레드 정지
	/// </summary>
	void stop();

	/// <summary>
	/// 소켓 상태 받아오는 함수, -1 정지요청, 0 정지, 1 돌아가는중
	/// </summary>
	/// <returns>상태</returns>
	inline int getSocketStatus()
	{
		return m_iStatus;
	}

	/// <summary>
	/// 소켓 정보 받아오는거
	/// </summary>
	/// <returns>m_SockInfo</returns>
	inline sockaddr_in* getSockinfo()
	{
		return &m_SockInfo;
	}

	/// <summary>
	/// 송신큐에 패킷 추가
	/// </summary>
	/// <param name="_lpAddrInfo">수신 또는 송신받을 대상</param>
	/// <param name="_iSize">데이터 크기</param>
	/// <param name="_lpData">데이터</param>
	inline void pushSend(int _iSize, char* _lpData)
	{
		cPacketTCP* pPacket = new cPacketTCP();
		pPacket->setData(_iSize, _lpData);
		mAMTX(m_mtxSendMutex);
		m_qSendQueue.push_back(pPacket);
	}

	/// <summary>
	/// 동적할당 되있는 패킷을 바로 송신큐에 넣는곳
	/// </summary>
	/// <param name="_pPacket">동적할당 되있는 패킷</param>
	inline void pushSend(cPacketTCP* _pPacket)
	{
		mAMTX(m_mtxSendMutex);
		m_qSendQueue.push_back(_pPacket);
	}

	/// <summary>
	/// 전역송신 할 때 사용하는 큐에있는 패킷 일괄 처리
	/// </summary>
	/// <param name="_pPacket">동적할당 되있는 패킷</param>
	inline void pushSend(std::deque<cPacketTCP*>* _lpPacketQueue)
	{
		mAMTX(m_mtxSendMutex);
		while(!_lpPacketQueue->empty())
		{
			//패킷을 그대로 복사해서 추가
			cPacketTCP* pFrontPacket = _lpPacketQueue->front();
			_lpPacketQueue->pop_front();
			cPacketTCP* pPacket = new cPacketTCP();
			pPacket->setData(pFrontPacket->m_iSize, pFrontPacket->m_pData, m_Sock);
			m_qSendQueue.push_back(pPacket);
		}
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

		mAMTX(m_mtxRecvMutex);
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

		mAMTX(m_mtxRecvMutex);
		std::swap(m_qRecvQueue, *_lpQueue);
		return true;
	}
};