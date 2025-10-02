#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <time.h>
#include <thread>
#include <iostream>

#include "CommonProtocol.h"

#include "Ring_Buffer.h"
#include "LFQMultiLive.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "CLanClient.h"
#include "CMonitorClient.h"

CMonitorClient::CMonitorClient(INT serverno) : m_ServerNo(serverno)
{

}

CMonitorClient::~CMonitorClient()
{
}

bool CMonitorClient::SendMonitorData(BYTE dataType, INT dataValue)
{
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear(1);
	*pPacket << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE;
	*pPacket << dataType;
	*pPacket << dataValue;
	*pPacket << (int)time(NULL);

	if (!SendPacket(pPacket))
	{
		CMessage::Free(pPacket);
		return false;
	}

	CMessage::Free(pPacket);

	return true;
}

void CMonitorClient::OnEnterJoinServer()
{
	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear(1);

	*pPacket << (WORD)en_PACKET_SS_MONITOR_LOGIN;
	*pPacket << m_ServerNo;

	SendPacket(pPacket);

	CMessage::Free(pPacket);
}

void CMonitorClient::OnLeaveServer()
{

}

void CMonitorClient::OnRecv(CMessage* pMessage)
{

}

void CMonitorClient::OnSend(int sendsize)
{
}
