#pragma once
//여기에서는 송수신 관련된 큐를 컨트롤한다
//송신큐 <-> 메인스레드
//수신큐 <-> 메인스레드
//모든 스레드는 뮤텍스로 잠금을 건다

//UDP는 패킷이 유실되도 모르기때문에 중요한 정보 주고받을때 쓰면 안됨
enum
{
	eSOCKET_STATUS_STOP = -1,	//스레드 정지중
	eSOCKET_STATUS_IDLE = 0,	//스레드 대기중
	eSOCKET_STATUS_RUN = 1,		//스레드 도는중
};

//패킷 처리하는곳
//송신대기큐(m_qSendWaitQueue)->송신큐(m_qSendQueue)->송신
//수신큐(m_qRecvQueue)->알아서 꺼내쓰기
class cUDPClient
{
private:
	std::mutex m_mtxSendMutex;					//송신 뮤텍스
	std::mutex m_mtxRecvMutex;					//수신 뮤텍스

	std::queue<cPacket*>	m_qSendQueue;		//송신 큐
	std::queue<cPacket*>	m_qSendWaitQueue;	//송신 대기 큐
	std::queue<cPacket*>	m_qRecvQueue;		//수신 큐
	std::thread* m_pSendThread;					//송신 스레드
	std::thread* m_pRecvThread;					//수신 스레드
	int		m_iStatus;							//상태 -1정지요청, 0정지, 1돌아가는중
	int		m_iPort;							//포트
	SOCKET	m_Sock;								//소캣
	sockaddr_in m_SockInfo;						//소캣 정보

private:
	/// <summary>
	/// 송신큐 대기큐에 있는걸 송신큐에 넣는 함수
	//	sendThread에서 있는 패킷 다 처리한다음에 호출해야 되는 부분
	/// </summary>
	void commitSendWaitQueue();

	/// <summary>
	/// 수신받을걸 수신큐에 넣는 함수
	/// </summary>
	/// <param name="_lpPacket">수신받은 패킷</param>
	void pushRecvQueue(cPacket* _lpPacket);

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

public:
	cUDPClient()
	{
		m_pSendThread = nullptr;	//송신 스레드
		m_pRecvThread = nullptr;	//수신 스레드
		m_iStatus = eSOCKET_STATUS_IDLE;//상태
		m_iPort = _DEFAULT_PORT;	//포트
	};

	/// <summary>
	/// 소캣 상태 받아오는 함수, -1 정지요청, 0 정지, 1 돌아가는중
	/// </summary>
	/// <returns></returns>
	inline int getSocketStatus() { return m_iStatus; }

	/// <summary>
	/// 소캣 시작
	/// </summary>
	/// <param name="_iPort">포트번호</param>
	void beginThread(bool _bIsServer, char* _csIP = nullptr, int _iPort = _DEFAULT_PORT);	//서버인지

	/// <summary>
	/// 통신 스레드 정지
	/// </summary>
	void stopThread();

	/// <summary>
	/// 송신큐에 패킷 추가
	/// </summary>
	/// <param name="_lpPacket">패킷</param>
	void pushSend(cPacket* _lpPacket);

	/// <summary>
	/// 수신큐 제일 앞 패킷 받아오기
	/// </summary>
	/// <returns>비어있으면 nullptr, 패킷이 있으면 패킷 포인터 반환</returns>
	cPacket* frontRecvQueue();

	/// <summary>
	/// 제일 앞 패킷 지우는거, 메모리 delete가 아닌 queue에서 지우는것을 의미
	/// </summary>
	void popRecvQueue();

	/// <summary>
	/// 수신 큐 초기화
	/// </summary>
	void resetRecvQueue();

	/// <summary>
	/// 수신 큐 내용물 자체를 복사
	/// </summary>
	/// <param name="_lpQueue">복사 뜰 버퍼</param>
	void copyRecvQueue(std::queue<cPacket*>* _lpQueue);

	sockaddr_in* getSockinfo()
	{
		return &m_SockInfo;
	}
};