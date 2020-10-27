#pragma once

//해당 지역에서만 락 걸고 자동으로 풀리게 만든거
class cAutoMutex
{
private:
	std::mutex* m_lpMutex;

public:
	cAutoMutex(std::mutex* _lpMutex)
	{
		m_lpMutex = _lpMutex;
		m_lpMutex->lock();
	}

	~cAutoMutex()
	{
		m_lpMutex->unlock();
	}
};

#define mAMTX(_lpMutex) cAutoMutex __CS__##_lpMutex(&_lpMutex)//자동으로 락 걸리고 지역변수가 해제될 때 자동으로 해제
