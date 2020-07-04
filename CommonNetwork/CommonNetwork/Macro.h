#pragma once

#define KILL(ptr) if(ptr != nullptr) delete ptr; ptr = nullptr;		//단일 삭제
#define pKILL(ptr) if(ptr != nullptr) delete[] ptr; ptr = nullptr;	//배열 삭제