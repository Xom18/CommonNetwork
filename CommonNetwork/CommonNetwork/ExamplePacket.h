#pragma once

enum
{
	ePACKET_TYPE_MESSAGE,
};

#pragma pack(push, 1)
//메세지 패킷
struct stPacketMessage : stPacketBase
{
	char m_aText[512];

	void resize()
	{
		m_iSize = static_cast<UINT32>(sizeof(stPacketBase) + strnlen_s(m_aText, sizeof(m_aText)) + 1);
	}
};
#pragma pack(pop)
