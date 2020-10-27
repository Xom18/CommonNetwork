#pragma once

#define KILL(ptr) if(ptr != nullptr) delete ptr; ptr = nullptr		//단일 삭제 세미콜론 빼먹으면 안되게 일부러 마지막에 세미콜론 뺌
#define pKILL(ptr) if(ptr != nullptr) delete[] ptr; ptr = nullptr	//배열 삭제 세미콜론 빼먹으면 안되게 일부러 마지막에 세미콜론 뺌