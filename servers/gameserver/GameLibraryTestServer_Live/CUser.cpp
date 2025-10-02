#include <windows.h>
#include "MemoryPoolTLS.h"
#include "IUser.h"
#include "CUser.h"

CMPoolTLS<CUser>* CUser::m_pUserPool = nullptr;

CUser::CUser()
{

}

CUser::~CUser()
{

}

void CUser::Clear(UINT64 uniqId, INT64 accountNo)
{
	m_uniqID = uniqId;
	m_accountNo = accountNo;
	m_timeOut = 0;
	m_recvTime = timeGetTime();

}

void CUser::Init()
{
	m_pUserPool = new CMPoolTLS<CUser>;
}

void CUser::Delete()
{
	delete m_pUserPool;
	m_pUserPool = nullptr;
}

CUser* CUser::UserAlloc()
{
	return m_pUserPool->Alloc();
}

void CUser::UserFree(CUser* pUser)
{
	m_pUserPool->Free(pUser);
}