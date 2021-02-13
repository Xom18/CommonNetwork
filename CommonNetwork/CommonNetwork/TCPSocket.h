#pragma once
//TCP 통신 서버 처리 하는곳
//TCP는 서버와 클라이언트가 꽤 달라서 코드 분리했음

//연결
//tryConnect->begin->recvThread(스레드), sendThread(스레드, 여기서 connectThread 해제)

//송신
//pushSend->sendThread(스레드)

//수신
//recvThread(스레드)->swapRecvQueue 또는 copyRecvQueue

//연결종료
//stop->stopThread(스레드)
class cTCPSocket
{
private:
	int		m_iStatus;							//상태 -1정지요청, 0정지, 1돌아가는중
	SOCKET	m_Sock;								//소켓
	unSOCKADDR_IN m_SockInfo;					//소켓 정보

	std::mutex m_mtxSendMutex;					//송신 뮤텍스
	std::mutex m_mtxRecvMutex;					//수신 뮤텍스
	std::condition_variable m_cvWaiter;			//대기용
	std::deque<cPacketTCP*>	m_qSendQueue;		//송신 큐
	std::deque<cPacketTCP*>	m_qRecvQueue;		//수신 큐
	std::thread* m_pRecvThread;					//수신 스레드
	std::thread* m_pSendThread;					//송신 스레드
	std::thread* m_pStoppingThread;				//중단 스레드

public:
	cTCPSocket()//생성자
	{
		m_pRecvThread = nullptr;	//수신 스레드
		m_pSendThread = nullptr;	//송신 스레드
		m_pStoppingThread = nullptr;//중단 스레드

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
	void stopThread();

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
	void setSocket(SOCKET _Sock, unSOCKADDR_IN* _Addr, int _iAddrLength, int* _lpMasterStatus)
	{
		m_Sock = _Sock;
		memcpy(&m_SockInfo, _Addr, _iAddrLength);
	}

	/// <summary>
	/// 클라이언트의 경우에 이쪽을 통해서 연결요청을 넣는다
	/// </summary>
	/// <param name="_csIP">IP</param>
	/// <param name="_iPort">포트(기본 58326)</param>
	bool tryConnectServer(const char* _csIP, const char* _csPort = _DEFAULT_PORT, int _iTimeOut = _DEFAULT_TIME_OUT, bool _bUseNoDelay = false);

	/// <summary>
	/// 스레드 정지
	/// </summary>
	void stop();

	/// <summary>
	/// 시작
	/// </summary>
	void begin();

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
	inline unSOCKADDR_IN* getSockinfo()
	{
		return &m_SockInfo;
	}

	/// <summary>
	/// 송신큐에 패킷 추가
	/// </summary>
	/// <param name="_lpAddrInfo">수신 또는 송신받을 대상</param>
	/// <param name="_iSize">데이터 크기</param>
	/// <param name="_lpData">데이터</param>
	inline void pushSend(int _iSize, const char* _lpData)
	{
		if (m_iStatus != eTHREAD_STATUS_RUN)
			return;

		cPacketTCP* pPacket = new cPacketTCP();
		pPacket->setData(_iSize, _lpData);
		{
			mAMTX(m_mtxSendMutex);
			m_qSendQueue.push_back(pPacket);
		}
		m_cvWaiter.notify_all();
	}

	/// <summary>
	/// 동적할당 되있는 패킷을 바로 송신큐에 넣는곳
	/// </summary>
	/// <param name="_pPacket">동적할당 되있는 패킷</param>
	inline void pushSend(cPacketTCP* _pPacket)
	{
		{
			mAMTX(m_mtxSendMutex);
			m_qSendQueue.push_back(_pPacket);
		}
		m_cvWaiter.notify_all();
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