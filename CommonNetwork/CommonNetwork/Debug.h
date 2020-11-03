#pragma once

//디버그용 매크로

#ifdef _DEBUG
#define mLOG(msg, ...)	printf("[%s\t%d]\t" msg "\n", __FUNCTION__, __LINE__, __VA_ARGS__);
#else
#define mLOG(msg, ...)	{}
#endif