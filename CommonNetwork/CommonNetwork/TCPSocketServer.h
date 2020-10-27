#pragma once
//TDP 통신 서버 처리 하는곳
//TDP는 서버와 클라이언트가 꽤 달라서 코드 분리했음

//사용법
//cUDPSocket 변수 선언
//beginThread 호출
//copyRecvQueue나 popRecvQueue를 통해서 패킷 꺼내가지고 처리한 뒤
//처리한 패킷 반드시 delete처리 필요, 수신받은 패킷은 다 동적할당 되있는거다

//주의사항
//UDP는 패킷이 유실되도 모르기때문에 중요한 정보 주고받을때 쓰면 안됨

//연결 처리하는곳
//송신큐와 송신 대기큐를 둠으로써 송신스레드에서는 송신할 때 매번 뮤텍스 호출을 안하고 패킷 빌때만 뮤텍스로 송신대기큐에서 당겨올때만 하면 된다
//송신큐만 대기큐가 있는이유는 수신은 copyRecvQueue로 외부 큐에서 가져가고 있기 때문에 
//pushSend호출->송신대기큐(m_qSendWaitQueue)->송신큐(m_qSendQueue)->송신
//수신큐(m_qRecvQueue)->copyRecvQueue호출->사용하는 프로그램에 맞게 꺼내쓰기, 반드시 꺼내쓰고 변수제거
class cTCPSocketServer
{
private:
	std::mutex m_mtxSendMutex;					//송신 뮤텍스
	std::mutex m_mtxRecvMutex;					//수신 뮤텍스
	std::mutex m_mtxConnectionMutex;			//연결 뮤텍스

	std::queue<cPacket*>	m_qSendQueue;		//송신 큐
	std::queue<cPacket*>	m_qSendWaitQueue;	//송신 대기 큐
	std::queue<cPacket*>	m_qRecvQueue;		//수신 큐
	std::queue<cPacket*>	m_qRecvWaitQueue;	//수신 대기큐

	std::thread* m_pConnectThread;				//연결 대기 스레드
	std::thread* m_pRecieveThread;				//수신 스레드(얘가 순회돌면서 패킷 가져옴)
	std::thread* m_pSendThread;					//송신 스레드(연결대상 전체를 향한 송신할 때 사용)
//	std::thread* m_pSendTargetThread;			//송신할 패킷 분배 스레드

	int		m_iStatus;							//상태 -1정지요청, 0정지, 1돌아가는중
	int		m_iPort;							//포트
	SOCKET	m_Sock;								//소켓
	sockaddr_in m_SockInfo;						//소켓 정보

	std::map<int, cTCPSocket*> m_mapTCPSocket;	//연결되있는 TCP 소켓들, 특정 대상 패킷처리 원활하게 인덱스로 잡혀있음
	std::queue<int> m_qUseEndIndex;				//사용 종료된 인덱스
public:
	cTCPSocketServer()//생성자
	{
		m_pConnectThread = nullptr;	//연결 대기 스레드
		m_pRecieveThread = nullptr;	//수신 스레드
		m_pSendThread = nullptr;	//전역 송신 스레드
		m_iStatus = eTHREAD_STATUS_IDLE;//상태
		m_iPort = _DEFAULT_PORT;	//포트
	};

	~cTCPSocketServer()//소멸자
	{
		stopThread();
	}

private:
	/// <summary>
	/// 연결 수립 스레드
	/// </summary>
	void connectThread();

	/// <summary>
	/// 송신 스레드
	/// 전역송신이 대상인 패킷만 여기서 처리
	/// </summary>
	void sendThread();

	/// <summary>
	/// 송신큐 대기큐에 있는걸 송신큐에 넣는 함수
	//	sendThread에서 있는 패킷 다 처리한다음에 호출해야 되는 부분
	/// </summary>
	inline void commitSendWaitQueue()
	{
		//혹시 송신대기로 올리는 중인게 있을 수 있으니 락
		mAMTX(m_mtxSendMutex);
		if(m_qSendWaitQueue.empty())
			return;
		std::swap(m_qSendQueue, m_qSendWaitQueue);
	}

	/// <summary>
	/// 수신받을걸 수신큐에 넣는 함수
	/// </summary>
	/// <param name="_lpPacket">수신받은 패킷</param>
	inline void pushRecvQueue(cPacket* _lpPacket)
	{
		mAMTX(m_mtxRecvMutex);
		m_qRecvQueue.push(_lpPacket);
	}
public:
	/// <summary>
	/// 소켓 시작
	/// </summary>
	/// <param name="_iPort">포트번호(기본 58326)</param>
	void beginThread(int _iPort = _DEFAULT_PORT, int _iTimeOut = _DEFAULT_TIME_OUT, bool _bUseNoDelay = false);

	/// <summary>
	/// 스레드 정지
	/// </summary>
	void stopThread();

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
	inline void sendAll(int _iSize, char* _lpData)
	{
		//UDP는 패킷 크기가 커질수록 도착할 확률이 낮아져서 일부러 작게함
		if(_iSize >= _MAX_TCP_DATA_SIZE)
		{
			printf("패킷 크기 너무 큼 %d\n", _iSize);
			return;
		}

		cPacket* pPacket = new cPacket();
		pPacket->setData(nullptr, _iSize, _lpData);
		mAMTX(m_mtxSendMutex);
		m_qSendWaitQueue.push(pPacket);
	}

	/// <summary>
	/// 수신 큐 내용물 자체를 복사
	/// </summary>
	/// <param name="_lpQueue">복사 뜰 버퍼</param>
	/// <param name="_lpQueue">수신 큐 초기화 여부 false 초기화 안함, true 초기화</param>
	inline void copyRecvQueue(std::queue<cPacket*>* _lpQueue, bool _bWithClear = true)
	{
		mAMTX(m_mtxRecvMutex);
		if(m_qRecvQueue.empty())
			return;
		*_lpQueue = m_qRecvQueue;

		//초기화 요청에 따른 초기화
		if(_bWithClear)
		{
			std::queue<cPacket*>	qRecvQueue;
			std::swap(m_qRecvQueue, qRecvQueue);
		}
	}
};