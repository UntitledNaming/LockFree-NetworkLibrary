#include <windows.h>

#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "Ring_Buffer.h"
#include "LFQMultiLive.h"
#include "CSession.h"

CSession::CSession()
{
	m_Socket = INVALID_SOCKET;
	m_SessionID = df_INVALID_SESSIONID;
	m_RefCnt = 0;
	m_SendMsgCnt = 0;
	m_SendFlag = 0;
	m_DCFlag = 0;
	m_RelFlag = 0;
}

CSession::~CSession()
{
}

void CSession::Init(SOCKET socket, UINT64 sessionID)
{

	m_Socket = socket;
	m_SessionID = sessionID;
	m_SendMsgCnt = 0;
	m_RecvQ.Clear();
	m_SendQ.Clear();

	InterlockedIncrement64(&m_RefCnt);
	InterlockedExchange16(&m_SendFlag, 0);
	InterlockedExchange(&m_DCFlag, 0);
	InterlockedExchange64(&m_RelFlag, 0);

}

