#pragma once

#define df_ID_LEN_MAX      20
#define df_NICK_LEN_MAX    20
#define df_NONUSER_TIMEOUT 3000
#define df_USER_TIMEOUT    40000


class CUser : public IUser
{
public:
	CUser();
	~CUser();

	void Clear(UINT64 uniqId, INT64 accountNo);



	static void   Init();
	static void   Delete();
	static CUser* UserAlloc();
	static void   UserFree(CUser* pUser);

public:
	INT64      m_accountNo;                   // 계정 번호
	UCHAR      m_timeOut;                     // 타임 아웃 체크 플래그
	DWORD      m_recvTime;                    // 메세지 마지막 수신 시간

public:
	static CMPoolTLS<CUser>* m_pUserPool;

};
