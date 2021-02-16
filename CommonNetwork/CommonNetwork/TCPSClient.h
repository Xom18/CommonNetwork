#pragma once

/// <summary>
/// IOCP에 사용하기 위한 구조체
/// </summary>
typedef struct _IO_DATA {
	OVERLAPPED	OL;
	WSABUF		buf;						//버퍼 포인터 = Buffer
	DWORD		sendlen;					//데이터 길이
	char		Buffer[_MAX_PACKET_SIZE];	//패킷 버퍼
	int			IOState;					//	0 - recv , 1 - send
}IO_DATA, * LP_IO_DATA;

/// <summary>
/// TCPSocketServer에서 include해서 사용하기때문에 다른곳에서 include 해서 쓰지 말것
/// </summary>
class cTCPSClient
{
private:
	std::mutex m_mtxClient;					//얘 자체 뮤텍스

	bool	m_bIsUse;						//사용중인지
	int		m_iIndex;						//인덱스
	SOCKET	m_Sock;							//소켓
	unSOCKADDR_IN m_SockInfo;				//소켓 정보

	std::deque<cPacketTCP*>	m_qSendQueue;	//송신 큐

	IO_DATA	m_RecvOL;						//수신 OL
	IO_DATA	m_SendOL;						//송신 OL

	bool	m_bIsSending;					//전송중인지

public:

private:

	/// <summary>
	/// 송신 큐 초기화
	/// 락 잡지 않습니다, 반드시 락 거는곳에서 호출 필요
	/// </summary>
	void resetSendQueue()
	{
		while (!m_qSendQueue.empty())
		{
			cPacketTCP* pPacket = m_qSendQueue.front();
			m_qSendQueue.pop_front();
			delete pPacket;
		}
	}

public:

	/// <summary>
	/// 생성자
	/// </summary>
	cTCPSClient()
	{
		m_bIsUse = false;
		memset(&m_SockInfo, 0, sizeof(m_SockInfo));
		memset(&m_RecvOL.OL, 0, sizeof(OVERLAPPED));
		ZeroMemory(m_RecvOL.Buffer, sizeof(m_RecvOL.Buffer));
		m_RecvOL.buf.buf = m_RecvOL.Buffer;
		m_RecvOL.buf.len = _MAX_PACKET_SIZE;
		m_RecvOL.IOState = 0;

		memset(&m_SendOL.OL, 0, sizeof(OVERLAPPED));
		ZeroMemory(m_SendOL.Buffer, sizeof(m_SendOL.Buffer));
		m_SendOL.buf.buf = m_SendOL.Buffer;
		m_SendOL.buf.len = _MAX_PACKET_SIZE;
		m_SendOL.IOState = 1;

		m_bIsSending = false;
	}

	/// <summary>
	/// 소멸자
	/// </summary>
	~cTCPSClient()
	{
		resetSendQueue();
	}

	/// <summary>
	/// 인덱스 설정
	/// </summary>
	/// <param name="_iIndex">인덱스</param>
	void setIndex(int _iIndex)
	{
		m_iIndex = _iIndex;
	}

	/// <summary>
	/// 인덱스 받아오기
	/// </summary>
	/// <returns>인덱스 값</returns>
	int getIndex()
	{
		return m_iIndex;
	}

	/// <summary>
	/// 활성 상태 설정
	/// </summary>
	void setUse()
	{
		mLG(m_mtxClient);
		resetSendQueue();
		m_bIsUse = true;
		m_bIsSending = false;
	}

	/// <summary>
	/// 비활성 상태 설정
	/// </summary>
	void setNotUse()
	{
		mLG(m_mtxClient);
		resetSendQueue();
		m_bIsUse = false;
		m_bIsSending = false;
	}

	/// <summary>
	/// 사용중인지
	/// </summary>
	/// <returns>사용중이면 true 아니면 false</returns>
	bool isUse()
	{
		return m_bIsUse;
	}

	/// <summary>
	/// 소켓 설정
	/// </summary>
	/// <param name="_Socket">소켓</param>
	void setSocket(SOCKET _Socket)
	{
		m_Sock = _Socket;
	}

	/// <summary>
	/// 소켓 받아오기
	/// </summary>
	/// <returns>소켓</returns>
	SOCKET getSocket()
	{
		return m_Sock;
	}

	/// <summary>
	/// 소켓 정보 설정
	/// </summary>
	/// <param name="_SockInfo">소켓 정보</param>
	void setInfo(unSOCKADDR_IN _SockInfo)
	{
		m_SockInfo = _SockInfo;
	}
	/// <summary>
	/// 소켓 정보 받아오기
	/// </summary>
	/// <returns>소켓 정보</returns>
	unSOCKADDR_IN getInfo()
	{
		return m_SockInfo;
	}

	/// <summary>
	/// 수신 OL 받아오기
	/// </summary>
	/// <returns>수신 OL</returns>
	IO_DATA* getRecvOL()
	{
		return &m_RecvOL;
	}

	/// <summary>
	/// 송신 OL 받아오기
	/// </summary>
	/// <returns>송신 OL</returns>
	IO_DATA* getSendOL()
	{
		return &m_SendOL;
	}

	/// <summary>
	/// 패킷 수신대기로 넘기기
	/// </summary>
	/// <returns>성공 여부</returns>
	bool recvPacket();

	/// <summary>
	/// 송신처리
	/// 송신 시도만 여기서 성공했는지 확인 가능하고 그 이후로는
	/// void cTCPServer::workThread() 쪽에서 실제로 송신에 성공했는지 별도로 확인해야됨
	/// </summary>
	/// <returns>성공 여부</returns>
	bool sendPacket();

	/// <summary>
	/// 송신 대기중인 다음 패킷 송신할 수 있게 준비
	/// </summary>
	/// <returns>송신 할 패킷이 더이상 없음</returns>
	bool pullNextPacket();

	/// <summary>
	/// 송신 할 패킷 추가
	/// </summary>
	/// <param name="_iSize">패킷 크기</param>
	/// <param name="_lpData">패킷</param>
	void addSendPacket(int _iSize, const char* _lpData);
};
