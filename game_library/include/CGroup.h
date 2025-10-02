#pragma once
#define df_DEFAULT_FRAME   50
#define df_INVALID_GROUPID -1

class CGameLibrary;

class CGroup
{
private:
	//////////////////////////////////////////////
	// 그룹 객체 변수
	//////////////////////////////////////////////
	CGameLibrary*       m_pGameLib;            // 게임 라이브러리 네트워크 및 그룹 등 함수 사용하기 위한 멤버
	SRWLOCK             m_GroupLock;           // 그룹을 상속 받는 컨텐츠 직렬 처리를 위한 Lock
	DWORD               m_GroupID;             // 그룹을 상속 받는 컨텐츠의 실제 타입 구분자
	UINT64              m_GroupFrameTime;      // 프레임 로직 돌릴때 프레임(ms로 변환)
	DWORD               m_OldTime;             // 프레임 스레드에서 체크할 시간
	BOOL                m_Shared;              // 멀티 스레드가 그룹에 접근 가능해도 되는지 제어 플래그


private:
	//////////////////////////////////////////////
	// 그룹 객체 모니터링 변수
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
	// 컨텐츠가 구현할 Callback 함수
	/////////////////////////////////////////////////////////////////////////////////////////
	virtual void  OnClientJoin(UINT64 sessionID) = 0;
	virtual void  OnClientLeave(UINT64 sessionID) = 0;
	virtual void  OnRecv(UINT64 sessionID, CMessage* pMessage) = 0;
	virtual void  OnIUserMove(UINT64 sessioID, IUser* pUser) = 0;
	virtual void  OnUpdate() = 0;

	/////////////////////////////////////////////////////////////////////////////////////////
	// 게임 라이브러리 클래스 제공함수 래핑
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
	// 그룹 모니터링 변수 Get,Set
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


