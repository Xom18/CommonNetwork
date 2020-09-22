#pragma once
//UDP ���ó�� �ϴ°�
//UDP�� ������ Ŭ���̾�Ʈ�� ũ�� �ٸ��� �ʾƼ� �׳� �����Ͽ� ó������

//����
//cUDPSocket ���� ����
//beginThread ȣ��
//copyRecvQueue�� popRecvQueue�� ���ؼ� ��Ŷ ���������� ó���� ��
//ó���� ��Ŷ �ݵ�� deleteó�� �ʿ�, ���Ź��� ��Ŷ�� �� �����Ҵ� ���ִ°Ŵ�

//���ǻ���
//UDP�� ��Ŷ�� ���ǵǵ� �𸣱⶧���� �߿��� ���� �ְ������ ���� �ȵ�

enum
{
	eSOCKET_STATUS_STOP = -1,	//������ ������
	eSOCKET_STATUS_IDLE = 0,	//������ �����
	eSOCKET_STATUS_RUN = 1,		//������ ������
};

//��Ŷ ó���ϴ°�
//�۽�ť�� �۽� ���ť�� �����ν� �۽Ž����忡���� �۽��� �� �Ź� ���ؽ� ȣ���� ���ϰ� ��Ŷ ������ ���ؽ��� �۽Ŵ��ť���� ��ܿö��� �ϸ� �ȴ�
//�۽�ť�� ���ť�� �ִ������� ������ copyRecvQueue�� �ܺ� ť���� �������� �ֱ� ������ 
//pushSendȣ��->�۽Ŵ��ť(m_qSendWaitQueue)->�۽�ť(m_qSendQueue)->�۽�
//����ť(m_qRecvQueue)->copyRecvQueueȣ��->����ϴ� ���α׷��� �°� ��������, �ݵ�� �������� ��������
class cUDPSocket
{
private:
	std::mutex m_mtxSendMutex;					//�۽� ���ؽ�
	std::mutex m_mtxRecvMutex;					//���� ���ؽ�

	std::queue<cPacket*>	m_qSendQueue;		//�۽� ť
	std::queue<cPacket*>	m_qSendWaitQueue;	//�۽� ��� ť
	std::queue<cPacket*>	m_qRecvQueue;		//���� ť
	std::thread* m_pSendThread;					//�۽� ������
	std::thread* m_pRecvThread;					//���� ������
	int		m_iStatus;							//���� -1������û, 0����, 1���ư�����
	int		m_iPort;							//��Ʈ
	SOCKET	m_Sock;								//��Ĺ
	sockaddr_in m_SockInfo;						//��Ĺ ����

public:
	cUDPSocket()//������
	{
		m_pSendThread = nullptr;	//�۽� ������
		m_pRecvThread = nullptr;	//���� ������
		m_iStatus = eSOCKET_STATUS_IDLE;//����
		m_iPort = _DEFAULT_PORT;	//��Ʈ
	};

	~cUDPSocket()//�Ҹ���
	{
		stopThread();
	}


private:
	/// <summary>
	/// ���� ������
	/// </summary>
	void recvThread();

	/// <summary>
	/// �۽� ������
	///	��ε�ĳ��Ʈ�� �����Ϸ� ������ WANȯ�濡�� ���ÿ���
	/// ������ ��Ʈ��ũ ��� �ʿ��ؼ� �̱���
	/// </summary>
	void sendThread();

	/// <summary>
	/// �۽�ť ���ť�� �ִ°� �۽�ť�� �ִ� �Լ�
	//	sendThread���� �ִ� ��Ŷ �� ó���Ѵ����� ȣ���ؾ� �Ǵ� �κ�
	/// </summary>
	inline void commitSendWaitQueue()
	{
		//Ȥ�� �۽Ŵ��� �ø��� ���ΰ� ���� �� ������ ��
		mAMTX(m_mtxSendMutex);
		if (m_qSendWaitQueue.empty())
			return;
		std::swap(m_qSendQueue, m_qSendWaitQueue);
	}

	/// <summary>
	/// ���Ź����� ����ť�� �ִ� �Լ�
	/// </summary>
	/// <param name="_lpPacket">���Ź��� ��Ŷ</param>
	inline void pushRecvQueue(cPacket* _lpPacket)
	{
		mAMTX(m_mtxRecvMutex);
		m_qRecvQueue.push(_lpPacket);
	}
public:
	/// <summary>
	/// ��Ĺ ����
	/// </summary>
	/// <param name="_bIsServer">�������� Ŭ������</param>
	/// <param name="_csIP">IP�ּ�, nullptr�̸� ADDR_ANY</param>
	/// <param name="_iPort">��Ʈ��ȣ</param>
	void beginThread(bool _bIsServer, char* _csIP = nullptr, int _iPort = _DEFAULT_PORT);

	/// <summary>
	/// ������ ����
	/// </summary>
	void stopThread();

	/// <summary>
	/// ��Ĺ ���� �޾ƿ��� �Լ�, -1 ������û, 0 ����, 1 ���ư�����
	/// </summary>
	/// <returns></returns>
	inline int getSocketStatus() 
	{ 
		return m_iStatus;
	}

	/// <summary>
	/// ��Ĺ ���� �޾ƿ��°�
	/// </summary>
	/// <returns>m_SockInfo</returns>
	inline sockaddr_in* getSockinfo()
	{
		return &m_SockInfo;
	}

	/// <summary>
	/// �۽�ť�� ��Ŷ �߰�
	/// </summary>
	/// <param name="_lpAddrInfo">���� �Ǵ� �۽Ź��� ���</param>
	/// <param name="_iSize">������ ũ��</param>
	/// <param name="_lpData">������</param>
	inline void pushSend(sockaddr_in* _lpAddrInfo, int _iSize, char* _lpData)
	{
		//UDP�� ��Ŷ ũ�Ⱑ Ŀ������ ������ Ȯ���� �������� �Ϻη� �۰���
		if (_iSize >= _MAX_UDP_DATA_SIZE)
		{
			printf("��Ŷ ũ�� �ʹ� ŭ %d\n", _iSize);
			return;
		}

		cPacket* pPacket = new cPacket();
		pPacket->setData(_lpAddrInfo, _iSize, _lpData);
		mAMTX(m_mtxSendMutex);
		m_qSendWaitQueue.push(pPacket);
	}

	/// <summary>
	/// ���� ť ���빰 ��ü�� ����
	/// </summary>
	/// <param name="_lpQueue">���� �� ����</param>
	/// <param name="_lpQueue">���� ť �ʱ�ȭ ���� false �ʱ�ȭ ����, true �ʱ�ȭ</param>
	inline void copyRecvQueue(std::queue<cPacket*>* _lpQueue, bool _bWithClear = true)
	{
		mAMTX(m_mtxRecvMutex);
		if (m_qRecvQueue.empty())
			return;
		*_lpQueue = m_qRecvQueue;

		//�ʱ�ȭ ��û�� ���� �ʱ�ȭ
		if (_bWithClear)
		{
			std::queue<cPacket*>	qRecvQueue;
			std::swap(m_qRecvQueue, qRecvQueue);
		}
	}
};