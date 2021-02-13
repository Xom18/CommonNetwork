#pragma once

#pragma pack(push, 1)
//패킷의 기본이 되는 정보
struct stPacketBase
{
	int m_iType;	//패킷 유형
	UINT32 m_iSize;	//패킷 크기

	void setInfo(int _iType, UINT32 _iSize)
	{
		m_iType = _iType;
		m_iSize = _iSize;
	}
};
#pragma pack(pop)

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
	void setData(int _iSize, const char* _lpData, unSOCKADDR_IN* _lpAddrInfo = nullptr)
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
	int m_iIndex;	//송신자 또는 수신자 인덱스
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
	void setData(int _iSize, const char* _lpData, int _iIndex = INT_MAX)
	{
		m_iIndex = _iIndex;
		m_iSize = _iSize;
		m_pData = new char[_iSize];
		ZeroMemory(m_pData, _iSize);
		memcpy(m_pData, _lpData, _iSize);
	}
};