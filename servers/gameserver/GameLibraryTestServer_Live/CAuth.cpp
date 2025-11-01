#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <timeapi.h>
#include <thread>

#include "TextParser.h"
#include "CommonProtocol.h"
#include "LogClass.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "IUser.h"
#include "CUser.h"

#include "LFQSingleLive.h"
#include "CGameLibrary.h"
#include "CGroup.h"
#include "CAuth.h"

CAuth::CAuth() 
{
	CUser::Init();
}

CAuth::~CAuth()
{
	CUser::Delete();
}

void CAuth::OnClientJoin(UINT64 sessionID)
{
	m_NonUserMap.insert(std::pair<UINT64, DWORD>(sessionID, timeGetTime()));
}

void CAuth::OnClientLeave(UINT64 sessionID)
{
	std::unordered_map<UINT64, DWORD>::iterator it = m_NonUserMap.find(sessionID);
	if (it == m_NonUserMap.end())
		__debugbreak();

	m_NonUserMap.erase(it);
}

void CAuth::OnRecv(UINT64 sessionID, CMessage* pMessage)
{
	WORD type;
	*pMessage >> type;

	if (pMessage->GetLastError())
	{
		Disconnect(sessionID);
		LOG(L"Auth", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CAuth::OnRecv CMessage Flag Error...  / UniqID : %lld ", sessionID);
		return;
	}

	switch (type)
	{
	case en_PACKET_CS_GAME_REQ_LOGIN:
		LoginRequsetProc(sessionID, pMessage);
		break;

	default:
		Disconnect(sessionID);
		LOG(L"Auth", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"CAuth::OnRecv Type Error...  / UniqID : %lld ", sessionID);
		break;
	}

	IncRecvTPS();
}

void CAuth::OnIUserMove(UINT64 sessioID, IUser* pUser)
{
	// 유저가 인증 컨텐츠로 올 수 없음. 그래서 발생하면 중단
	__debugbreak();
}

void CAuth::OnUpdate()
{
	// NonUser 타임아웃
	//std::unordered_map<UINT64, DWORD>::iterator it = m_NonUserMap.begin();
	//for (; it != m_NonUserMap.end(); ++it)
	//{
	//	if (timeGetTime() - it->second >= AUTH_TIMEOUT)
	//	{
	//		Disconnect(it->first);
	//	}
	//}

	IncFrameTPS();
}

void CAuth::LoginRequsetProc(UINT64 sessionID, CMessage* pMessage)
{
	CUser* pUser = nullptr;
	INT64 accountNo;
	CHAR sessionkey[df_SESSIONKEY_MAX];
	int  version;

	*pMessage >> accountNo;
	pMessage->GetData(sessionkey, df_SESSIONKEY_MAX);
	*pMessage >> version;


	if (pMessage->GetLastError())
	{
		LOG(L"Auth", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"EchoRequest::CMessage Flag Error... / UniqID : %lld", sessionID);
		//프로토콜 보다 보낸 데이터 크기가 적으면 플래그 켜짐.
		Disconnect(sessionID);
		return;
	}

	if (pMessage->GetDataSize() > 0)
	{
		LOG(L"Auth", en_LOG_LEVEL::dfLOG_LEVEL_DEBUG, L"EchoRequest::CMessage Size Overflow Error... / UniqID : %lld ", sessionID);
		//프로토콜 보다 보낸 데이터 크기가 크면 끊기
		Disconnect(sessionID);
		return;
	}

	// NonUser 자료구조에서 제거
	m_NonUserMap.erase(sessionID);

	// 유저 객체 생성
	pUser = CUser::UserAlloc();
	pUser->Clear(sessionID, accountNo);

	// 그룹 이동
	if (!GroupMove(L"Echo", sessionID, dynamic_cast<IUser*>(pUser)))
	{
		// 그룹 이동 실패시(그 사이에 세션이 무효화 된 경우)
		CUser::UserFree(pUser);
	}


	return;
}


