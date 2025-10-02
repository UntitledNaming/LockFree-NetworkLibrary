#pragma once
#define df_DEFAULT_FRAME   50
#define df_INVALID_GROUPID -1

class CGameLibrary;

class CGroup
{
private:
	//////////////////////////////////////////////
	// �׷� ��ü ����
	//////////////////////////////////////////////
	CGameLibrary*       m_pGameLib;            // ���� ���̺귯�� ��Ʈ��ũ �� �׷� �� �Լ� ����ϱ� ���� ���
	SRWLOCK             m_GroupLock;           // �׷��� ��� �޴� ������ ���� ó���� ���� Lock
	DWORD               m_GroupID;             // �׷��� ��� �޴� �������� ���� Ÿ�� ������
	UINT64              m_GroupFrameTime;      // ������ ���� ������ ������(ms�� ��ȯ)
	DWORD               m_OldTime;             // ������ �����忡�� üũ�� �ð�
	BOOL                m_Shared;              // ��Ƽ �����尡 �׷쿡 ���� �����ص� �Ǵ��� ���� �÷���


private:
	//////////////////////////////////////////////
	// �׷� ��ü ����͸� ����
	//////////////////////////////////////////////
	LONG                               m_RecvTPS;
	LONG                               m_SendTPS;
	LONG                               m_FrameTPS;

public:
	CGroup();
	virtual ~CGroup();

	inline DWORD  GetOldTime()
	{
		return m_OldTime;
	}
	inline void   SetOldTime()
	{
		InterlockedAdd((LONG*)&m_OldTime, m_GroupFrameTime);
	}

	inline DWORD GetGroupID()
	{
		return m_GroupID;
	}

	inline void   SetGroupID(DWORD groupid)
	{
		m_GroupID = groupid;
	}

    inline UINT64  GetGroupFrame()
	{
		return m_GroupFrameTime;
	}

	inline void SetGroupFrame(UINT64 groupFrame)
	{
		m_GroupFrameTime = groupFrame;
	}

	inline void   SetGameLib(CGameLibrary* plb)
	{
		m_pGameLib = plb;
	}
	inline void   ExclusiveGroupLock()
	{
		AcquireSRWLockExclusive(&m_GroupLock);
	}
	inline void   ExclusiveGroupUnlock()
	{
		ReleaseSRWLockExclusive(&m_GroupLock);
	}

	inline void SharedGroupLock()
	{
		AcquireSRWLockShared(&m_GroupLock);
	}

	inline void SharedGroupUnlock()
	{
		ReleaseSRWLockShared(&m_GroupLock);
	}

	inline BOOL GetSharedFlag()
	{
		return m_Shared;
	}

	inline void SetSharedFlag(BOOL flag)
	{
		m_Shared = flag;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// �������� ������ Callback �Լ�
	/////////////////////////////////////////////////////////////////////////////////////////
	virtual void  OnClientJoin(UINT64 sessionID) = 0;
	virtual void  OnClientLeave(UINT64 sessionID) = 0;
	virtual void  OnRecv(UINT64 sessionID, CMessage* pMessage) = 0;
	virtual void  OnIUserMove(UINT64 sessioID, IUser* pUser) = 0;
	virtual void  OnUpdate() = 0;

	/////////////////////////////////////////////////////////////////////////////////////////
	// ���� ���̺귯�� Ŭ���� �����Լ� ����
	/////////////////////////////////////////////////////////////////////////////////////////
	inline bool    SendPacket(UINT64 SessionID, CMessage* pMessage)
	{
		return     m_pGameLib->SendPacket(SessionID, pMessage);
	}
	inline bool    Disconnect(UINT64 SessionID)
	{
		return     m_pGameLib->Disconnect(SessionID);
	}
	inline bool    FindIP(UINT64 SessionID, std::wstring& OutIP)
	{
		return     m_pGameLib->FindIP(SessionID, OutIP);
	}
	
	inline bool    GroupMove(std::wstring ToContents, UINT64 sessionID, IUser* pUser)
	{
		return m_pGameLib->GroupMove(ToContents, sessionID, pUser);
	}
	inline LONG    GetAcceptTPS()
	{
		return m_pGameLib->GetAcceptTPS();
	}
	inline LONG    GetRecvIOTPS()
	{
		return m_pGameLib->GetRecvIOTPS();
	}
	inline LONG    GetSendIOTPS()
	{
		return m_pGameLib->GetSendIOTPS();
	}
	inline INT64   GetAcceptTotal()
	{
		return m_pGameLib->GetAcceptTotal();
	}
	inline SHORT   GetCurSessionCount()
	{
		return m_pGameLib->GetCurSessionCount();
	}
	inline void    SetAcceptTPS(LONG value)
	{
		m_pGameLib->SetAcceptTPS(value);
	}
	inline void    SetRecvIOTPS(LONG value)
	{
		m_pGameLib->SetRecvIOTPS(value);
	}
	
	inline void    SetSendIOTPS(LONG value)
	{
		m_pGameLib->SetSendIOTPS(value);
	}
	
	inline CGroup* GetGroupPtr(std::wstring Contents)
	{
		return m_pGameLib->GetGroupPtr(Contents);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// �׷� ����͸� ���� Get,Set
	/////////////////////////////////////////////////////////////////////////////////////////
	inline LONG GetRecvTPS()
	{
		return m_RecvTPS;
	}

	inline void SetRecvTPS(LONG value)
	{
		m_RecvTPS = value;
	}

	inline void IncRecvTPS()
	{
		m_RecvTPS++;
	}

	inline LONG GetSendTPS()
	{
		return m_SendTPS;
	}

	inline void SetSendTPS(LONG value)
	{
		m_SendTPS = value;
	}

	inline void IncSendTPS()
	{
		m_SendTPS++;
	}

	inline LONG GetFrameTPS()
	{
		return m_FrameTPS;
	}

	inline void SetFrameTPS(LONG value)
	{
		m_FrameTPS = value;
	}

	inline void IncFrameTPS()
	{
		m_FrameTPS++;
	}

};


