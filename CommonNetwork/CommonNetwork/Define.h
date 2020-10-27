#pragma once

#define _DEFAULT_PORT 58326		//기본 포트
#define _MAX_PACKET_SIZE 65535	//패킷 최대 크기
#define _MAX_UDP_DATA_SIZE 512	//UDP(IPv4) 최대 데이터 길이
#define _MAX_UDP_IPv6_DATA_SIZE 1024 //UDP(IPv6) 최대 데이터 길이
#define _MAX_TCP_DATA_SIZE 512	//TCP 최대 데이터 길이
#define _DEFAULT_TIME_OUT 5000	//기본 타임아웃

enum
{
	eTHREAD_STATUS_STOP = -1,	//스레드 정지중
	eTHREAD_STATUS_IDLE = 0,	//스레드 대기중
	eTHREAD_STATUS_RUN = 1,		//스레드 도는중
};