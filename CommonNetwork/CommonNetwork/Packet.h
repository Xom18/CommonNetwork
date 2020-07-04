#pragma once

/// <summary>
/// ���Ź��� ��Ŷ�� ���⿡ �߽��� ������ ũ��, ��Ŷ�� ����ִ�
/// </summary>
class cPacket
{
public:
	sockaddr_in m_AddrInfo;	//�۽��� �Ǵ� ������
	int m_iSize;			//������ ũ��
	char* m_pData;			//������

	~cPacket()
	{
		pKILL(m_pData);
	}

	/// <summary>
	/// ���Ź��� ��Ŷ�� �־�δ� �Լ�
	/// </summary>
	/// <param name="_lpAddrInfo">�۽��� �Ǵ� ������ ����</param>
	/// <param name="_iSize">������ ũ��</param>
	/// <param name="_lpData">������</param>
	void setData(sockaddr_in* _lpAddrInfo, int _iSize, char* _lpData)
	{
		memcpy(&m_AddrInfo, _lpAddrInfo, sizeof(sockaddr_in));
		m_iSize = _iSize;
		m_pData = new char[_iSize];
		ZeroMemory(m_pData, _iSize);
		memcpy(m_pData, _lpData, _iSize);
	}
};