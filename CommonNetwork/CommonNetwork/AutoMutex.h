#pragma once

//�ش� ���������� �� �ɰ� �ڵ����� Ǯ���� �����
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

#define mAMTX(_lpMutex) cAutoMutex __CS__##_lpMutex(&_lpMutex)//�ڵ����� �� �ɸ��� ���������� ������ �� �ڵ����� ����
