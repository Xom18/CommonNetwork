#pragma once

#define KILL(ptr) if(ptr != nullptr) delete ptr; ptr = nullptr;		//���� ����
#define pKILL(ptr) if(ptr != nullptr) delete[] ptr; ptr = nullptr;	//�迭 ����