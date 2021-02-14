#include <deque>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include <list>
#include <condition_variable>
#include <set>
#include <vector>
#include "Debug.h"
#include "Macro.h"
#include "Define.h"
#include "Packet.h"
#include "TCPSClient.h"

bool cTCPSClient::recvPacket()
{
	DWORD	dwRecv;
	DWORD	Flags = 0;
	if (WSARecv(m_Sock, &m_RecvOL.buf, 1, &dwRecv, &Flags, &m_RecvOL.OL, NULL) == SOCKET_ERROR)
	{
		DWORD dwError = WSAGetLastError();

		if (dwError != WSA_IO_PENDING && dwError != ERROR_SUCCESS)
			return false;
	}

	return true;
}

//전송 대기중인 다음 패킷 가져오는거
bool cTCPSClient::pullNextPacket()
{
	cPacketTCP* pPacket = nullptr;

	{
		mLG(m_mtxSendMutex);
		if (m_qSendQueue.empty())
			return false;

		pPacket = m_qSendQueue.front();
		m_qSendQueue.pop_front();
	}
	m_SendOL.sendlen = pPacket->m_iSize;
	memcpy(m_SendOL.Buffer, pPacket->m_pData, pPacket->m_iSize);
	delete pPacket;

	return true;
}
void cTCPSClient::addSendPacket(int _iSize, const char* _lpData)
{
	//사용중이지 않음
	if (!m_bIsUse)
		return;

	//현재 송신중이면 큐에 넣어놓고
	if (m_bIsSending)
	{
		cPacketTCP* pNewPacket = new cPacketTCP();
		pNewPacket->setData(_iSize, _lpData);

		mLG(m_mtxSendMutex);
		m_qSendQueue.push_back(pNewPacket);
	}
	else
	{//송신중이 아니면 바로 전송시도
		memcpy(m_SendOL.Buffer, _lpData, _iSize);
		m_SendOL.sendlen = _iSize;
		sendPacket();
	}
}

bool cTCPSClient::sendPacket()
{
	//사용중이지 않음
	if (!m_bIsUse)
		return false;

	//전송상태 true로 전환
	m_bIsSending = true;

	DWORD	dwSend;

	if (WSASend(m_Sock, &m_SendOL.buf, 1, &dwSend, 0, &m_SendOL.OL, NULL) == SOCKET_ERROR)
	{
		DWORD dwError = WSAGetLastError();

		if (dwError != WSA_IO_PENDING && dwError != ERROR_SUCCESS)
			return false;
	}

	return true;
}
