#pragma once

#define df_AUTH_TIMEOUT   3000 
#define df_SESSIONKEY_MAX 64

class CGroup;
class CMessage;

class CAuth : public CGroup
{

public:
	CAuth();
	~CAuth();

	inline size_t GetUserCount()
	{
		return m_NonUserMap.size();
	}

private:
	virtual void  OnClientJoin(UINT64 sessionID);
	virtual void  OnClientLeave(UINT64 sessionID);
	virtual void  OnRecv(UINT64 sessionID, CMessage* pMessage);
	virtual void  OnIUserMove(UINT64 sessioID, IUser* pUser);
	virtual void  OnUpdate();


	void          LoginRequsetProc(UINT64 sessionID, CMessage* pMessage);
private:
	std::unordered_map<UINT64, DWORD> m_NonUserMap;

};

