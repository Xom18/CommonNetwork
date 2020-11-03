#pragma once
//UDP 통신처리 하는곳
//UDP는 서버와 클라이언트가 크게 다르지 않아서 그냥 한파일에 처리했음

//연결
//begin->recvThread(스레드), sendThread(스레드)

//송신
//pushSend->sendThread(스레드)

//수신
//recvThread(스레드)->swapRecvQueue 또는 copyRecvQueue

//연결종료
//stop

class cUDPSocket
{
private:
	std::mutex m_mtxSendMutex;					//송신 뮤텍스
	std::mutex m_mtxRecvMutex;					//수신 뮤텍스

	std::deque<cPacketUDP*>	m_qSendQueue;		//송신 큐
	std::deque<cPacketUDP*>	m_qRecvQueue;		//수신 큐
	std::thread* m_pSendThread;					//송신 스레드
	std::thread* m_pRecvThread;					//수신 스레드
	std::thread* m_pStoppingThread;				//중단 스레드
	int		m_iStatus;							//상태 -1정지요청, 0정지, 1돌아가는중
	int		m_iPort;							//포트
	SOCKET	m_Sock;								//소켓
	sockaddr_in m_SockInfo;						//소켓 정보

public:
	cUDPSocket()//생성자
	{
		m_pSendThread = nullptr;	//송신 스레드
		m_pRecvThread = nullptr;	//수신 스레드
		m_pStoppingThread = nullptr;//중단 스레드
		m_iStatus = eTHREAD_STATUS_IDLE;//상태
		m_iPort = _DEFAULT_PORT;	//포트
		m_Sock = INVALID_SOCKET;
		ZeroMemory(&m_SockInfo, sizeof(m_SockInfo));
	};

	~cUDPSocket()//소멸자
	{
		stop();
		if(m_pStoppingThread != nullptr)
			m_pStoppingThread->join();
		KILL(m_pStoppingThread);
	}


private:
	/// <summary>
	/// 수신 스레드
	/// </summary>
	void recvThread();

	/// <summary>
	/// 송신 스레드
	///	브로드캐스트를 지원하려 했으나 WAN환경에서 사용시에는
	/// 별도의 네트워크 장비가 필요해서 미구현
	/// </summary>
	void sendThread();

	/// <summary>
	/// 수신받을걸 수신큐에 넣는 함수
	/// </summary>
	/// <param name="_lpPacket">수신받은 패킷</param>
	inline void pushRecvQueue(cPacketUDP* _lpPacket)
	{
		mAMTX(m_mtxRecvMutex);
		m_qRecvQueue.push_back(_lpPacket);
	}

	/// <summary>
	/// 중단 마무리 스레드
	/// </summary>
	void stoppingThread();

public:
	/// <summary>
	/// 소켓 시작
	/// </summary>
	/// <param name="_bIsServer">서버인지 클라인지</param>
	/// <param name="_csIP">IP주소, nullptr이면 ADDR_ANY</param>
	/// <param name="_iPort">포트번호</param>
	bool begin(bool _bIsServer, char* _csIP = nullptr, int _iPort = _DEFAULT_PORT, int _iTimeOut = _DEFAULT_TIME_OUT);

	/// <summary>
	/// 스레드 정지
	/// </summary>
	void stop();

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
	inline void pushSend(int _iSize, char* _lpData, sockaddr_in* _lpAddrInfo = nullptr)
	{
		//UDP는 패킷 크기가 커질수록 도착할 확률이 낮아져서 일부러 작게함
		if (_iSize >= _MAX_UDP_DATA_SIZE)
		{
			printf("패킷 크기 너무 큼 %d\n", _iSize);
			return;
		}

		cPacketUDP* pPacket = new cPacketUDP();
		pPacket->setData(_iSize, _lpData, _lpAddrInfo);
		mAMTX(m_mtxSendMutex);
		m_qSendQueue.push_back(pPacket);
	}

	/// <summary>
	/// 수신된 패킷 큐에 있는걸 받아오는거
	/// </summary>
	/// <param name="_lpQueue">복사 뜰 비어있는 queue 변수</param>
	/// <param name="_bFlush">수신 큐 초기화 여부</param>
	inline bool getRecvQueue(std::deque<cPacketUDP*>* _lpQueue, bool _bFlush = true)
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
	inline bool swapRecvQueue(std::deque<cPacketUDP*>* _lpQueue)
	{
		if(m_qRecvQueue.empty())
			return false;

		mAMTX(m_mtxRecvMutex);
		std::swap(m_qRecvQueue, *_lpQueue);
		return true;
	}
};