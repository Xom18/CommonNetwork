#pragma once

#ifdef _DEBUG
#define mLOG(msg, ...)	printf("[%s : %d]" msg "\n", __FUNCTION__, __LINE__, __VA_ARGS__);
#else
#define mLOG(msg, ...)	{}
#endif