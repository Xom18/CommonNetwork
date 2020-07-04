#pragma once

/// <summary>
/// 수신받은 패킷은 여기에 발신자 정보와 크기, 패킷을 담고있다
/// </summary>
class cPacket
{
public:
	sockaddr_in m_AddrInfo;	//송신자 또는 수신자
	int m_iSize;			//데이터 크기
	char* m_pData;			//데이터

	~cPacket()
	{
		pKILL(m_pData);
	}

	/// <summary>
	/// 수신받은 패킷을 넣어두는 함수
	/// </summary>
	/// <param name="_lpAddrInfo">송신자 또는 수신자 정보</param>
	/// <param name="_iSize">데이터 크기</param>
	/// <param name="_lpData">데이터</param>
	void setData(sockaddr_in* _lpAddrInfo, int _iSize, char* _lpData)
	{
		memcpy(&m_AddrInfo, _lpAddrInfo, sizeof(sockaddr_in));
		m_iSize = _iSize;
		m_pData = new char[_iSize];
		ZeroMemory(m_pData, _iSize);
		memcpy(m_pData, _lpData, _iSize);
	}
};