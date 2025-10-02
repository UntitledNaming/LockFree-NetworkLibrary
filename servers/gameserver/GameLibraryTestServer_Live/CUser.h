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
	INT64      m_accountNo;                   // ���� ��ȣ
	UCHAR      m_timeOut;                     // Ÿ�� �ƿ� üũ �÷���
	DWORD      m_recvTime;                    // �޼��� ������ ���� �ð�

public:
	static CMPoolTLS<CUser>* m_pUserPool;

};
