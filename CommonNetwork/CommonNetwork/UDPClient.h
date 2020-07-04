#pragma once
//���⿡���� �ۼ��� ���õ� ť�� ��Ʈ���Ѵ�
//�۽�ť <-> ���ν�����
//����ť <-> ���ν�����
//��� ������� ���ؽ��� ����� �Ǵ�

//UDP�� ��Ŷ�� ���ǵǵ� �𸣱⶧���� �߿��� ���� �ְ������ ���� �ȵ�
enum
{
	eSOCKET_STATUS_STOP = -1,	//������ ������
	eSOCKET_STATUS_IDLE = 0,	//������ �����
	eSOCKET_STATUS_RUN = 1,		//������ ������
};

//��Ŷ ó���ϴ°�
//�۽Ŵ��ť(m_qSendWaitQueue)->�۽�ť(m_qSendQueue)->�۽�
//����ť(m_qRecvQueue)->�˾Ƽ� ��������
class cUDPClient
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

private:
	/// <summary>
	/// �۽�ť ���ť�� �ִ°� �۽�ť�� �ִ� �Լ�
	//	sendThread���� �ִ� ��Ŷ �� ó���Ѵ����� ȣ���ؾ� �Ǵ� �κ�
	/// </summary>
	void commitSendWaitQueue();

	/// <summary>
	/// ���Ź����� ����ť�� �ִ� �Լ�
	/// </summary>
	/// <param name="_lpPacket">���Ź��� ��Ŷ</param>
	void pushRecvQueue(cPacket* _lpPacket);

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

public:
	cUDPClient()
	{
		m_pSendThread = nullptr;	//�۽� ������
		m_pRecvThread = nullptr;	//���� ������
		m_iStatus = eSOCKET_STATUS_IDLE;//����
		m_iPort = _DEFAULT_PORT;	//��Ʈ
	};

	/// <summary>
	/// ��Ĺ ���� �޾ƿ��� �Լ�, -1 ������û, 0 ����, 1 ���ư�����
	/// </summary>
	/// <returns></returns>
	inline int getSocketStatus() { return m_iStatus; }

	/// <summary>
	/// ��Ĺ ����
	/// </summary>
	/// <param name="_iPort">��Ʈ��ȣ</param>
	void beginThread(bool _bIsServer, char* _csIP = nullptr, int _iPort = _DEFAULT_PORT);	//��������

	/// <summary>
	/// ��� ������ ����
	/// </summary>
	void stopThread();

	/// <summary>
	/// �۽�ť�� ��Ŷ �߰�
	/// </summary>
	/// <param name="_lpPacket">��Ŷ</param>
	void pushSend(cPacket* _lpPacket);

	/// <summary>
	/// ����ť ���� �� ��Ŷ �޾ƿ���
	/// </summary>
	/// <returns>��������� nullptr, ��Ŷ�� ������ ��Ŷ ������ ��ȯ</returns>
	cPacket* frontRecvQueue();

	/// <summary>
	/// ���� �� ��Ŷ ����°�, �޸� delete�� �ƴ� queue���� ����°��� �ǹ�
	/// </summary>
	void popRecvQueue();

	/// <summary>
	/// ���� ť �ʱ�ȭ
	/// </summary>
	void resetRecvQueue();

	/// <summary>
	/// ���� ť ���빰 ��ü�� ����
	/// </summary>
	/// <param name="_lpQueue">���� �� ����</param>
	void copyRecvQueue(std::queue<cPacket*>* _lpQueue);

	sockaddr_in* getSockinfo()
	{
		return &m_SockInfo;
	}
};