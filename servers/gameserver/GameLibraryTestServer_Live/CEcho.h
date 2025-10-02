#pragma once
#define df_ECHO_TIMEOUT 40000

class CGroup;
class IUser;
class CUser;
class CMessage;

class CEcho : public CGroup
{
public:
	CEcho();
	~CEcho();

	inline size_t GetUserCount()
	{
		return m_EchoUser.size();
	}

private:
	virtual void  OnClientJoin(UINT64 sessionID);
	virtual void  OnClientLeave(UINT64 sessionID);
	virtual void  OnRecv(UINT64 sessionID, CMessage* pMessage);
	virtual void  OnIUserMove(UINT64 sessioID, IUser* pUser);
	virtual void  OnUpdate();

private:
	void EchoRequestProc(UINT64 sessionID, CMessage* pMessage);
	void HearBeatProc(UINT64 sessionID, CMessage* pMessage);

	void LoginResponse(CUser* pUser);


private:
	std::unordered_map<UINT64, CUser*> m_EchoUser;
};