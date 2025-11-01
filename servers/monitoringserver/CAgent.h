#pragma once

class CAgent
{
public:
	CAgent();
	~CAgent();

	void         Agent_Init(UINT64 uniqid);

public:
	UINT64  m_uniqID;
	DWORD   m_recvTime;
	bool    m_timeOut;
};

