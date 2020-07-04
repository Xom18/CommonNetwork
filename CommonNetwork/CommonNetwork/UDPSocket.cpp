#include <queue>
#include <mutex>
#include <WS2tcpip.h>
#include <map>
#include "Macro.h"
#include "Define.h"
#include "AutoMutex.h"
#include "Packet.h"
#include "UDPSocket.h"

void cUDPSocket::recvThread()
{
	fd_set FdSet;
	struct timeval tv;

	// ��� ���� ����
	FD_ZERO(&FdSet);
	FD_SET(m_Sock, &FdSet);

	tv.tv_sec = 10;	//10�� Ÿ�Ӿƿ�
	tv.tv_usec = 0;	//�и��ʴ� ����

	char* pRecvBuffer = new char[_MAX_PACKET_SIZE];	//������ ����
	int ClientAddrLength = sizeof(sockaddr_in);

	printf("Begin recvThread %d\n", m_iPort);

	while (m_iStatus == eSOCKET_STATUS_RUN)
	{
		sockaddr_in Client;
		ZeroMemory(pRecvBuffer, _MAX_PACKET_SIZE);	//�ʱ�ȭ
		ZeroMemory(&Client, sizeof(Client));


		//Ÿ�Ӿƿ��̰ų� �����Ͱ� �԰ų� �����ų�, �����Ϳ����� 1, Ÿ�Ӿƿ� 0, ���� -1
		int iSelectResult = select((int)m_Sock, &FdSet, NULL, NULL, &tv);
		if (iSelectResult != 1)
			continue;


		//������ ����
		int iDataLength = recvfrom((int)m_Sock, pRecvBuffer, _MAX_PACKET_SIZE, 0, (sockaddr*)&Client, &ClientAddrLength);
		if (iDataLength == SOCKET_ERROR)
		{
//			printf("���ſ���\n");
			continue;
		}

		//���� ������ ó��
		cPacket* pPacket = new cPacket();
		pPacket->setData(&Client, iDataLength, pRecvBuffer);

		//ť�� �ִ´�
		pushRecvQueue(pPacket);

		//�۽��� IP������ �־�а�, �ּ�
		//char clientIP[256];
		//ZeroMemory(clientIP, sizeof(clientIP));
		//inet_ntop(AF_INET, &client.sin_addr, clientIP, sizeof(clientIP));
		//printf("%s : %s\n", clientIP, pRecvBuffer);
	}

	pKILL(pRecvBuffer);

	printf("End recvThread\n");
}

void cUDPSocket::sendThread()//�۽� ������
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
			memcpy(pSendBuffer, lpPacket->m_pData, iDataSize);
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
void cUDPSocket::beginThread(bool _bIsServer, char* _csIP, int _iPort)
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

	if (_csIP == nullptr)
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
void cUDPSocket::stopThread()
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