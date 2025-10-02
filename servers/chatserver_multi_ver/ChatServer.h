#pragma once
#define df_TIMEOUT1        3000      // 3초 타임아웃
#define df_TIMEOUT2        40000     // 30초 타임아웃
#define df_FRAME           10

class CMonitorClient;
class ProcessMonitor;

template <typename T>
class CMemoryPool;

template <typename T>
class LFQueue;

class CUser;



class ChatServer : public CNetServer
{
private:

	struct st_SECTOR_LIST
	{
		SRWLOCK          s_lock;
		std::list<CUser*> s_list;
	}typedef SECTORLIST;

private:
	////////////////////////////////////////////////////////////////////////////
	// ChatServer 자료 구조 및 멤버 객체
	////////////////////////////////////////////////////////////////////////////
	std::unordered_map<UINT64, CUser*>  m_UserMap;
	std::unordered_map<UINT64, DWORD>   m_NonUserMap;
	SECTORLIST                          m_Sector[SECTOR_Y_MAX][SECTOR_X_MAX];
	SRWLOCK                             m_UserMapLock;
	SRWLOCK                             m_NonUserMapLock;
	CMemoryPool<CUser>*                 m_pUserPool;
	ProcessMonitor*                     m_pPDH;
	CMonitorClient*                     m_pMonitorClient;  // 모니터링 서버 접속 클라이언트
	BOOL                                m_EndFlag;         // 모니터,  스레드 종료 플래그


private:
	///////////////////////////////////////////////////////////////////////////
	// ChatServer 스레드
	///////////////////////////////////////////////////////////////////////////
	std::thread                         m_Frame;
	std::thread                         m_Monitor;

private:
	///////////////////////////////////////////////////////////////////////////
    // ChatServer 모니터링 변수
    ///////////////////////////////////////////////////////////////////////////
	LONG                                m_ReqTPS;        // 초당 메세지 응답 갯수(섹터 이동, 채팅 메세지, 로그인 요청 합본)
	LONG                                m_ReqLoginTPS;   // 초당 메세지 응답 갯수(로그인 요청에 대한 응답)
	LONG                                m_ResLoginTPS;   // 초당 메세지 응답 갯수(로그인 요청에 대한 응답)
	LONG                                m_ReqMoveTPS;    // 초당 메세지 응답 갯수(섹터 이동 요청에 대한 응답)
	LONG                                m_ResMoveTPS;    // 초당 메세지 응답 갯수(섹터 이동 요청에 대한 응답)
	LONG                                m_ReqChatMsgTPS; // 초당 메세지 응답 갯수(채팅 보내기 요청에 대한 응답, 주변 섹터 유저에게 SendPacket할때 마다 카운팅)
	LONG                                m_ResChatMsgTPS; // 초당 메세지 응답 갯수(채팅 보내기 요청에 대한 응답, 주변 섹터 유저에게 SendPacket할때 마다 카운팅)


	///////////////////////////////////////////////////////////////////////////
    // ChatServer Config 변수
    ///////////////////////////////////////////////////////////////////////////
	INT                                 m_UserMaxCnt;

public:
	ChatServer();
	~ChatServer();

	BOOL         RunServer();
	void         StopServer();

private:
	void         Mem_Init(INT userMAX,WCHAR* bindip, WCHAR* serverip, INT serverport);
	void         Thread_Create();
	void         Thread_Destroy();
		         
		         
	UINT64       SumResMsgTPS();

	///////////////////////////////////////////////////////////////////////////
    // CNetServer Callback 함수 구현
    ///////////////////////////////////////////////////////////////////////////
	virtual bool OnConnectionRequest(WCHAR* InputIP, USHORT InputPort);
	virtual void OnClientJoin(UINT64 SessionID);
	virtual void OnClientLeave(UINT64 SessionID);
	virtual void OnRecv(UINT64 SessionID, CMessage* pMessage);

	///////////////////////////////////////////////////////////////////////////
	// 타임아웃 함수
	///////////////////////////////////////////////////////////////////////////
	void NonUserTimeOut();
	void UserTimeOut();

	///////////////////////////////////////////////////////////////////////////
	// 섹터 관련 함수
	///////////////////////////////////////////////////////////////////////////
	void AcquireSectorExclusiveLock(WORD eXpos, WORD eYpos, WORD iXpos, WORD iYpos);
	void ReleaseSectorExclusiveLock(WORD eXpos, WORD eYpos, WORD iXpos, WORD iYpos);
	void SendPacket_SectorOne(CMessage* pMessage, WORD xpos, WORD ypos, CUser* pUser);
	void SendPakcet_SectorAround(CMessage* pMessage, CUser* pUser);
	void SectorFind(SECTOR_AROUND* pAround, WORD xpos, WORD ypos);
	BOOL SectorRangeCheck(WORD xpos, WORD ypos);

	///////////////////////////////////////////////////////////////////////////
    // Recv Message 핸들러
    ///////////////////////////////////////////////////////////////////////////
	void LoginProc(CMessage* pMessage, UINT64 sessionid);
	void SectorMoveProc(CMessage* pMessage, UINT64 sessionid);
	void ChatMessageProc(CMessage* pMessage, UINT64 sessionid);
	void HeartBeatProc(UINT64 sessionid);

	void FrameThread();
	void MonitorThread();
};