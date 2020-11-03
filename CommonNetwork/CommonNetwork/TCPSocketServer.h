#pragma once
//TCP 통신 서버 처리 하는곳
//TCP는 서버와 클라이언트가 꽤 달라서 코드 분리했음

//시작
//begin->connectThread(스레드), operateThread(스레드)

//연결
//connectThread(스레드)->cTCPSocket 동적할당->setSocket->begin->

//송신
//sendAll->연결되있는 TCPSocket들에 pushSend->sendThread(스레드)->
//sendTarget->특정 대상 TCPSocket에 pushSend->sendThread(스레드)->

//수신
//recvThread(스레드)->swapRecvQueue 또는 copyRecvQueue

//연결종료
//stop

class cTCPSocketServer
{
private:
	std::mutex m_mtxGlobalSendMutex;			//송신 뮤텍스(전체)
	std::mutex m_mtxTargetSendMutex;			//송신 뮤텍스(전체)
	std::mutex m_mtxRecvMutex;					//수신 뮤텍스
	std::mutex m_mtxConnectionMutex;			//연결 대기 뮤텍스
	std::mutex m_mtxSocketMutex;				//연결 뮤텍스

	std::deque<cPacketTCP*>	m_qGlobalSendQueue;	//전역 송신 큐
	std::deque<cPacketTCP*>	m_qTargetSendQueue;	//대상 송신 큐
	std::deque<cPacketTCP*>	m_qRecvQueue;		//수신 큐

	std::thread* m_pConnectThread;				//연결 대기 스레드
	std::thread* m_pOperateThread;				//처리 스레드(패킷수신, 전역송신)

	int		m_iStatus;							//상태 -1정지요청, 0정지, 1돌아가는중
	int		m_iPort;							//포트
	SOCKET	m_Sock;								//소켓
	sockaddr_in m_SockInfo;						//소켓 정보

	std::map<SOCKET, cTCPSocket*> m_mapTCPSocket;	//연결되있는 TCP 소켓들, 특정 대상 패킷처리 원활하게 인덱스로 잡혀있음
	//연결 대기 큐
	std::queue<cTCPSocket*> m_qConnectWait;	//연결 대기 큐

public:
	cTCPSocketServer()//생성자
	{
		m_pConnectThread = nullptr;	//연결 대기 스레드
		m_pOperateThread = nullptr;	//수신 스레드
		m_iStatus = eTHREAD_STATUS_IDLE;//상태
		m_iPort = _DEFAULT_PORT;	//포트
		m_Sock = INVALID_SOCKET;
		ZeroMemory(&m_SockInfo, sizeof(m_SockInfo));
	};

	~cTCPSocketServer()//소멸자
	{
		stop();
	}

private:
	/// <summary>
	/// 연결 수립 스레드
	/// </summary>
	void connectThread();

	/// <summary>
	/// 처리 스레드
	/// 패킷 가져오고 전역패킷 보내고 처리
	/// </summary>
	void operateThread();

	inline void operateConnectWait()
	{
		//연결 대기중인거 다 밀어넣기
		if(!m_qConnectWait.empty())
		{
			std::queue<cTCPSocket*> qConnectWait;
			{
				mAMTX(m_mtxConnectionMutex);
				std::swap(m_qConnectWait, qConnectWait);
			}

			mAMTX(m_mtxSocketMutex);
			while(!qConnectWait.empty())
			{
				cTCPSocket* lpSocket = qConnectWait.front();
				qConnectWait.pop();
				m_mapTCPSocket.insert(std::pair<SOCKET, cTCPSocket*>(lpSocket->getSocket(), lpSocket));
			}
		}
	}

public:
	/// <summary>
	/// 소켓 시작
	/// </summary>
	/// <param name="_iPort">포트번호(기본 58326)</param>
	void begin(int _iPort = _DEFAULT_PORT, int _iTimeOut = _DEFAULT_TIME_OUT, bool _bUseNoDelay = false);

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
	inline void sendAll(int _iSize, char* _lpData)
	{
		cPacketTCP* pPacket = new cPacketTCP();
		pPacket->setData(_iSize, _lpData);
		mAMTX(m_mtxGlobalSendMutex);
		m_qGlobalSendQueue.push_back(pPacket);
	}

	/// <summary>
	/// 특정 대상에게 송신하는 패킷
	/// </summary>
	/// <param name="_Socket">대상 소캣</param>
	/// <param name="_iSize">데이터 크기</param>
	/// <param name="_lpData">데이터</param>
	/// <returns></returns>
	inline void sendTarget(SOCKET _Socket, int _iSize, char* _lpData)
	{
		cPacketTCP* pPacket = new cPacketTCP();
		pPacket->setData(_iSize, _lpData, _Socket);
		mAMTX(m_mtxTargetSendMutex);
		m_qTargetSendQueue.push_back(pPacket);
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