#pragma once

#define KILL(ptr) if(ptr != nullptr) delete ptr; ptr = nullptr		//���� ���� �����ݷ� �������� �ȵǰ� �Ϻη� �������� �����ݷ� ��
#define pKILL(ptr) if(ptr != nullptr) delete[] ptr; ptr = nullptr	//�迭 ���� �����ݷ� �������� �ȵǰ� �Ϻη� �������� �����ݷ� ��