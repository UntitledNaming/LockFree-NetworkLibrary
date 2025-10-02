#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <unordered_map>
#include <string>
#include <thread>

#include "CommonProtocol.h"
#include "LogClass.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "IUser.h"
#include "CUser.h"

#include "LFQSingleLive.h"
#include "CGameLibrary.h"
#include "CGroup.h"
#include "CEcho.h"

CEcho::CEcho()
{

}

CEcho::~CEcho()
{
}

void CEcho::OnClientJoin(UINT64 sessionID)
{

}

void CEcho::OnClientLeave(UINT64 sessionID)
{
	CUser* pUser = nullptr;
	std::unordered_map<UINT64, CUser*>::iterator it = m_EchoUser.find(sessionID);
	pUser = it->second;
	m_EchoUser.erase(sessionID);


	CUser::UserFree(pUser);

}

void CEcho::OnRecv(UINT64 sessionID, CMessage* pMessage)
{
	// 에코 처리
	WORD type;
	*pMessage >> type;

	if (pMessage->GetLastError())
	{
		Disconnect(sessionID);
		LOG(L"CEcho", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CEcho::OnRecv CMessage Flag Error...  \ UniqID : %lld ", sessionID);
		return;
	}

	switch (type)
	{
	case en_PACKET_CS_GAME_REQ_ECHO:
		EchoRequestProc(sessionID, pMessage);
		break;

	case en_PACKET_CS_GAME_REQ_HEARTBEAT:
		HearBeatProc(sessionID, pMessage);
		break;

	default:
		Disconnect(sessionID);
		LOG(L"CEcho", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CEcho::OnRecv Message Type Error...  \ UniqID : %lld ", sessionID);
		break;
	}

	IncRecvTPS();
}

void CEcho::OnIUserMove(UINT64 sessioID, IUser* pUser)
{
	m_EchoUser.insert(std::pair<UINT64, CUser*>(sessioID, dynamic_cast<CUser*>(pUser)));

	LoginResponse(dynamic_cast<CUser*>(pUser));

}

void CEcho::OnUpdate()
{
	//CUser* pUser = nullptr;

	// User 타임아웃
	//std::unordered_map<UINT64, CUser*>::iterator it = m_EchoUser.begin();
	//for (; it != m_EchoUser.end(); ++it)
	//{
	//	pUser = it->second;
	//	if (timeGetTime() - pUser->s_RecvTime >= ECHO_TIMEOUT)
	//	{
	//		if (pUser->s_TimeOut == 0)
	//		{
	//			Disconnect(it->first);
	//			pUser->s_TimeOut = 1;
	//		}
	//	}
	//}

	IncFrameTPS();
}

void CEcho::EchoRequestProc(UINT64 sessionID, CMessage* pMessage)
{
	INT64 accountNo;
	LONGLONG tick;
	CUser* pUser = nullptr;

	// 메세지 뽑고 에러 체크

	*pMessage >> accountNo;
	*pMessage >> tick;

	if (pMessage->GetLastError())
	{
		LOG(L"Echo", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"EchoRequest::CMessage Flag Error... \ UniqID : %lld", sessionID);
		//프로토콜 보다 보낸 데이터 크기가 적으면 플래그 켜짐.
		Disconnect(sessionID);
		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"Echo", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"EchoRequest::CMessage Size Overflow Error... \ UniqID : %lld ", sessionID);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		Disconnect(sessionID);
		return;
	}

	// 에코 응답 생성
	std::unordered_map<UINT64, CUser*>::iterator it = m_EchoUser.find(sessionID);
	if (it == m_EchoUser.end())
		__debugbreak();

	pUser = it->second;
	pUser->m_recvTime = timeGetTime();

	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();

	*pPacket << (WORD)en_PACKET_CS_GAME_RES_ECHO;
	*pPacket << pUser->m_accountNo;
	*pPacket << tick;

	SendPacket(sessionID, pPacket);

	CMessage::Free(pPacket);

	IncSendTPS();
}

void CEcho::HearBeatProc(UINT64 sessionID, CMessage* pMessage)
{
	CUser* pUser = nullptr;

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"Echo", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"EchoRequest::CMessage Size Overflow Error... \ UniqID : %lld ", sessionID);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		Disconnect(sessionID);
		return;
	}

	std::unordered_map<UINT64, CUser*>::iterator it = m_EchoUser.find(sessionID);
	if (it == m_EchoUser.end())
		__debugbreak();

	pUser = it->second;
	pUser->m_recvTime = timeGetTime();

}

void CEcho::LoginResponse(CUser* pUser)
{

	CMessage* pPacket = CMessage::Alloc();
	pPacket->Clear();

	*pPacket << (WORD)en_PACKET_CS_GAME_RES_LOGIN;
	*pPacket << (BYTE)1; // 1 : 성공, 0 : 실패
	*pPacket << pUser->m_accountNo;

	SendPacket(pUser->m_uniqID, pPacket);

	CMessage::Free(pPacket);

	IncSendTPS();
}


