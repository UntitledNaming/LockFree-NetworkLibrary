#pragma once
#define MONITOR_SESSION_KEY_MAX             32
#define df_AGENTSMANAGER_NONAGENT_TIMEOUT    3000
#define df_AGENTSMANAGER_AGENT_TIMEOUT       40000

class CAgent;

class CMonAgentsMgr : public CNetServer
{
public:
	CMonAgentsMgr();
	~CMonAgentsMgr();

	void   RunAgentsManager(INT MAXAGENTCNT, WCHAR* SERVERIP, INT SERVERPORT, INT numberOfCreateThread, INT numberOfRunningThread, INT maxNumOfSession, INT SendSleep, INT SendTHFL, WORD packetCode, WORD fixedkey, BOOL OffNagle);
	void   StopAgentsManager();

	void   SendServerData(BYTE serverNo, BYTE dataType, INT dataValue, INT timeStamp);
	size_t GetAgentsSize();
	size_t GetNonAgentsSize();

private:
	virtual bool  OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort) override;

	virtual void  OnClientJoin(UINT64 SessionID) override;

	virtual void  OnClientLeave(UINT64 SessionID) override;

	virtual void  OnRecv(UINT64 SessionID, CMessage* pMessage) override;

	        void  ToolLoginProc(UINT64 sessionID, CMessage* pMessage);

			void  AgentInsert(UINT64 sessionID, CAgent* pUser);

			void  FrameThread();

			///////////////////////////////////////////////////////////////////////////
            // 타임아웃 함수
            ///////////////////////////////////////////////////////////////////////////
			void  NonAgentTimeOut();
			void  AgentTimeOut();

private:
	const CHAR*                         m_monitorSessionKey; // 모니터링 툴 로그인 키
	INT                                 m_agentMaxCnt;
	std::unordered_map<UINT64, CAgent*> m_agentMap;
	std::unordered_map<UINT64, DWORD>   m_nonAgentMap;
	std::thread                         m_frame;
	SRWLOCK                             m_agentMapLock;
	SRWLOCK                             m_nonAgentMapLock;
	CMemoryPool<CAgent>*                m_pAgentPool;
	BOOL                                m_endFlag;
};
