#include <queue>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "UDPClient.h"

void cUDPClient::commitSendWaitQueue()
{
	//Ȥ�� �۽Ŵ��� �ø��� ���ΰ� ���� �� ������ ��
	mAMTX(m_mtxSendMutex);
	if (m_qSendWaitQueue.empty())
		return;
	std::swap(m_qSendQueue, m_qSendWaitQueue);
}

void cUDPClient::recvThread()
{
	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];
	sockaddr_in client;
	int iClientLength = sizeof(client);

	printf("Begin recvThread %d\n", m_iPort);

	while (m_iStatus == eSOCKET_STATUS_RUN)
	{
		ZeroMemory(pRecvBuffer, _MAX_PACKET_SIZE);	//�ʱ�ȭ
		ZeroMemory(&client, iClientLength);

		//������ ����
		int iDataLength = recvfrom(m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0, (sockaddr*)&client, &iClientLength);
		if (iDataLength == SOCKET_ERROR)
		{
			printf("���ſ���\n");
			continue;
		}

		//���� ������ ó��
		cPacket* pPacket = new cPacket();
		pPacket->setData(&client, iDataLength, pRecvBuffer);

		//ť�� �ִ´�
		pushRecvQueue(pPacket);

		//�۽��� IP������ �־�а�, �ּ�
		//char clientIP[256];
		//ZeroMemory(clientIP, sizeof(clientIP));
		//inet_ntop(AF_INET, &client.sin_addr, clientIP, sizeof(clientIP));
		//printf("%s\n", clientIP);
	}

	pKILL(pRecvBuffer);

	printf("End recvThread\n");
}

void cUDPClient::pushRecvQueue(cPacket* _lpPacket)
{
	mAMTX(m_mtxSendMutex);
	m_qRecvQueue.push(_lpPacket);
}

void cUDPClient::sendThread()//�۽� ������
{
	printf("Begin sendThread %d\n", m_iPort);

	char* pSendBuffer = new char[_MAX_PACKET_SIZE];	//�۽��� ��Ŷ ����
	while (m_iStatus == eSOCKET_STATUS_RUN)
	{
		if (m_qSendQueue.empty())
		{//��������� ���ť�� �����´�
			commitSendWaitQueue();

			//���ť���� ��������� 10ms���� ���
			if (m_qSendQueue.empty())
				Sleep(10);
			continue;
		}

		ZeroMemory(pSendBuffer, _MAX_PACKET_SIZE);//������ �ʱ�ȭ
		sockaddr_in AddrInfo;	//������ ����
		int iDataSize = 0;		//������ ũ��

		{//������ ���� ��Ŷ�� ���´�
			cPacket* lpPacket = m_qSendQueue.front();
			iDataSize = lpPacket->m_iSize;
			memcpy(&AddrInfo, &lpPacket->m_AddrInfo, sizeof(AddrInfo));
			memcpy(pSendBuffer, &lpPacket->m_pData, iDataSize);
			//�������� �ٷ� ����
			m_qSendQueue.pop();
			delete lpPacket;
		}


		if (sendto(m_Sock, pSendBuffer, iDataSize, 0, (sockaddr*)&AddrInfo, sizeof(AddrInfo)) == SOCKET_ERROR)
		{//�۽Ž����ϸ� ����
			printf("�۽� ����\n");
			continue;
		}
	}

	pKILL(pSendBuffer);
	printf("End sendThread\n");
}

//������ ����
void cUDPClient::beginThread(bool _bIsServer, char* _csIP, int _iPort)
{
	if (m_pSendThread != nullptr
	|| m_pRecvThread != nullptr)
	{
		printf("�̹� �������� �����尡 �ִ�\n");
		return;
	}

	WSADATA wsaData;							//���� ������
	WORD wVersion = MAKEWORD(1, 0);				//����
	int iWSOK = WSAStartup(wVersion, &wsaData);	//��Ĺ ����
	if (iWSOK != 0)
	{
		printf("��Ĺ ���� ����\n");
		ExitProcess(EXIT_FAILURE);
	}

	m_iPort = _iPort;									//��Ʈ
	m_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);	//��Ĺ ����

	if(_csIP == nullptr)
		m_SockInfo.sin_addr.S_un.S_addr = ADDR_ANY;		//��ü ���
	else
		inet_pton(AF_INET, _csIP, &m_SockInfo.sin_addr);//Ư�� IP�� ���
	m_SockInfo.sin_family = AF_INET;					//UDP ���
	m_SockInfo.sin_port = htons(m_iPort);				//��Ʈ

	//������� ���ε� �ʿ�
	if (_bIsServer)
	{
		//���ε�, ���� ������ ��Ʈ�� �̹� ���������� ������
		if (bind(m_Sock, (sockaddr*)&m_SockInfo, sizeof(sockaddr_in)) != 0)
		{
			printf("���ε� ����[%d]\n", WSAGetLastError());
			ExitProcess(EXIT_FAILURE);
		}
	}

	//���� �����ϰ� ������ ����
	m_iStatus = eSOCKET_STATUS_RUN;
	m_pRecvThread = new std::thread([&]() {recvThread(); });
	m_pSendThread = new std::thread([&]() {sendThread(); });
}

//������ ����
void cUDPClient::stopThread()
{
	//�����尡 ���������� �ǹ̾����� return
	if (m_iStatus == eSOCKET_STATUS_IDLE)
		return;

	//������ ���߰� ���� �ٲ���
	m_iStatus = eSOCKET_STATUS_STOP;

	//������ ���� ���
	m_pSendThread->join();
	m_pRecvThread->join();

	//���� ����
	KILL(m_pSendThread);
	KILL(m_pSendThread);

	//���� �缳��
	m_iStatus = eSOCKET_STATUS_IDLE;

	//��Ĺ ����
	closesocket(m_Sock);

	//	WSACleanup();
}

//�۽�ť�� �־����
void cUDPClient::pushSend(cPacket* _lpPacket)
{
	//UDP�� ��Ŷ ũ�Ⱑ Ŀ������ ������ Ȯ���� �������� �Ϻη� �۰���
	if (_lpPacket->m_iSize >= _MAX_UDP_DATA_SIZE)
	{
		printf("��Ŷ ũ�� �ʹ� ŭ %d\n", _lpPacket->m_iSize);
		return;
	}

	m_qSendWaitQueue.push(_lpPacket);
}

//����ť ���� �� ���빰
cPacket* cUDPClient::frontRecvQueue()
{
	mAMTX(m_mtxSendMutex);
	if (m_qRecvQueue.empty())
		return nullptr;

	cPacket* Packet = m_qRecvQueue.front();
	return Packet;
}

//����ť ���� �� ���빰 ������
void cUDPClient::popRecvQueue()
{
	mAMTX(m_mtxSendMutex);
	if (m_qRecvQueue.empty())
		return;

	m_qRecvQueue.pop();
}

//���� ť �ʱ�ȭ
void cUDPClient::resetRecvQueue()
{
	mAMTX(m_mtxRecvMutex);
	if (m_qRecvQueue.empty())
		return;

	std::queue<cPacket*>	qRecvQueue;
	std::swap(m_qRecvQueue, qRecvQueue);
}

//���� ť ���빰 ����
void cUDPClient::copyRecvQueue(std::queue<cPacket*>* _lpQueue)
{
	mAMTX(m_mtxRecvMutex);
	*_lpQueue = m_qRecvQueue;
}