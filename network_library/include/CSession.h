#pragma once
#define df_SERVER_WSABUFSIZE  100
#define df_INVALID_SESSIONID -1

class CSession
{

public:
	CSession();
	~CSession();

	void Init(SOCKET socket, UINT64 sessionID);

public:
	SOCKET                 m_Socket;
	UINT64                 m_SessionID;
	OVERLAPPED             m_RecvOverlapped;
	OVERLAPPED             m_SendOverlapped;
	CRingBuffer            m_RecvQ;
	LFQueueMul<CMessage*>  m_SendQ;
	CMessage*              m_SendArray[df_SERVER_WSABUFSIZE];

	SHORT                  m_SendFlag;
	INT                    m_SendMsgCnt;
	LONG                   m_DCFlag;
	alignas(16)LONGLONG    m_RefCnt;
	LONGLONG               m_RelFlag;
};


