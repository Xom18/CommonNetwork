#pragma once

/// <summary>
/// 수신받은 패킷은 여기에 발신자 정보와 크기, 패킷을 담고있다
/// </summary>
class cPacketUDP
{
public:
	unSOCKADDR_IN m_AddrInfo;//송신자 또는 수신자
	int m_iSize;			//데이터 크기
	char* m_pData;			//데이터

	~cPacketUDP()
	{
		pKILL(m_pData);
	}

	/// <summary>
	/// 수신받은 패킷을 넣어두는 함수
	/// </summary>
	/// <param name="_iSize">데이터 크기</param>
	/// <param name="_lpData">데이터</param>
	/// <param name="_lpAddrInfo">송신자 또는 수신자 정보</param>
	void setData(int _iSize, char* _lpData, unSOCKADDR_IN* _lpAddrInfo = nullptr)
	{
		if(_lpAddrInfo != nullptr)
			memcpy(&m_AddrInfo, _lpAddrInfo, sizeof(unSOCKADDR_IN));
		m_iSize = _iSize;
		m_pData = new char[_iSize];
		ZeroMemory(m_pData, _iSize);
		memcpy(m_pData, _lpData, _iSize);
	}
};

/// <summary>
/// TCP용 패킷
/// </summary>
class cPacketTCP
{
public:
	SOCKET m_Sock;	//송신자 또는 수신자
	int m_iSize;	//데이터 크기
	char* m_pData;	//데이터

	~cPacketTCP()
	{
		pKILL(m_pData);
	}

	/// <summary>
	/// 수신받은 패킷을 넣어두는 함수
	/// </summary>
	/// <param name="_iSize">데이터 크기</param>
	/// <param name="_lpData">데이터</param>
	/// <param name="_lpAddrInfo">송신자 또는 수신자 정보</param>
	void setData(int _iSize, char* _lpData, SOCKET _iSerial = INVALID_SOCKET)
	{
		m_Sock = _iSerial;
		m_iSize = _iSize;
		m_pData = new char[_iSize];
		ZeroMemory(m_pData, _iSize);
		memcpy(m_pData, _lpData, _iSize);
	}
};