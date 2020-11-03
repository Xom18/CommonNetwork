#pragma once

//어디 한곳에 놓기 애매한 매크로

#define KILL(ptr) {if(ptr != nullptr) delete ptr; ptr = nullptr;}		//단일 삭제
#define pKILL(ptr) {if(ptr != nullptr) delete[] ptr; ptr = nullptr;}	//배열 삭제