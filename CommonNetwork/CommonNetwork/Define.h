#pragma once

//디파인들

#define _DEFAULT_PORT "58326"	//기본 포트
#define _MAX_PACKET_SIZE 32767	//패킷 최대 크기
#define _MAX_UDP_DATA_SIZE 512	//UDP(IPv4) 최대 데이터 길이
#define _MAX_UDP_IPv6_DATA_SIZE 1024 //UDP(IPv6) 최대 데이터 길이
#define _MAX_TCP_SEGMENT_SIZE 512	//TCP는 패킷 최대크기가 없으나 Maximum segment size 라는게 있다
#define _MAX_TCP_IPv6_SEGMENT_SIZE 1024	//TCP는 패킷 최대크기가 없으나 Maximum segment size 라는게 있다
#define _DEFAULT_TIME_OUT 5000	//기본 타임아웃
#define _DEFAULT_TICK 15	//TCP 서버 처리 간격 15ms
#define _IP_LENGTH 64		//IP를 텍스트로 출력 할 때 쓰는 버퍼 길이
#define _MAX_TCP_CLIENT_COUNT 1024
#define _MAX_SEND_PACKET_ONCE	10 //클라이언트에서 한번에 보낼 수 있는 최대 패킷 수

enum
{
	eTHREAD_STATUS_STOP = -1,	//스레드 정지중
	eTHREAD_STATUS_IDLE = 0,	//스레드 대기중
	eTHREAD_STATUS_RUN = 1,		//스레드 도는중
};

enum
{
	eWINSOCK_USE_IPv4 = 1,	//IPv4 전용
	eWINSOCK_USE_IPv6 = 2,	//IPv6 전용
	eWINSOCK_USE_BOTH = 3,	//둘다 사용
};

union unSOCKADDR_IN
{
	sockaddr_in IPv4;
	sockaddr_in6 IPv6;
};