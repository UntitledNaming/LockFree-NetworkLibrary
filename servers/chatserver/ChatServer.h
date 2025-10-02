#pragma once
#define PTR_BITMASK     0x00007FFFFFFFFFFF
#define TIMEOUT1        3000      // 3초 타임아웃
#define TIMEOUT2        40000     // 30초 타임아웃
#define MESSAGE_LEN_MAX 256
#define SESSION_KEY_MAX 64


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
	enum JobType
	{
		// Update 스레드 Job Type
		en_Join = 0,
		en_Recv,
		en_Delete,
		en_End,
	};

	struct st_JOB
	{
		UINT64     s_Id;
		WORD       s_Type;
		CMessage*  s_ptr;   

	}typedef JOB;

private:
	////////////////////////////////////////////////////////////////////////////
	// ChatServer 자료 구조 및 멤버 객체
	////////////////////////////////////////////////////////////////////////////
	std::unordered_map<UINT64, CUser*>  m_UserMap;
	std::unordered_map<UINT64, DWORD>   m_NonUserMap;
	std::list<CUser*>                   m_Sector[SECTOR_Y_MAX][SECTOR_X_MAX];
	LFQueue<JOB*>*                      m_pUpdateJobQ;
	CMemoryPool<CUser>*                 m_pUserPool;
	CMemoryPool<JOB>*                   m_pJobPool;
	ProcessMonitor*                     m_pPDH;
	CMonitorClient*                     m_pMonitorClient;  // 모니터링 서버 접속 클라이언트
	BOOL                                m_MonitorFlag;     // 모니터링 스레드 종료 플래그


private:
	///////////////////////////////////////////////////////////////////////////
	// ChatServer 스레드
	///////////////////////////////////////////////////////////////////////////
	std::thread                         m_Update;
	std::thread                         m_Monitor;

private:
	///////////////////////////////////////////////////////////////////////////
    // ChatServer 모니터링 변수
    ///////////////////////////////////////////////////////////////////////////
	LONG                                m_UpdateLoopTime;// 서버 루프간 시간 차이
	LONG                                m_UpdateTPS;
	LONG                                m_ResTPS;        // 초당 메세지 응답 갯수(로그인 요청, 섹터 이동, 메세지 요청 등)
	LONG                                m_ReqTPS;        // 초당 메세지 요청 갯수(섹터 이동, 채팅 메세지, 로그인 요청 합본)
	LONG                                m_ReqLoginTPS;   // 초당 메세지 요청 갯수(로그인 요청에 대한 응답)
	LONG                                m_ResLoginTPS;   // 초당 메세지 응답 갯수(로그인 요청에 대한 응답)
	LONG                                m_ReqMoveTPS;    // 초당 메세지 요청 갯수(섹터 이동 요청에 대한 응답)
	LONG                                m_ResMoveTPS;    // 초당 메세지 응답 갯수(섹터 이동 요청에 대한 응답)
	LONG                                m_ReqChatMsgTPS; // 초당 메세지 요청 갯수(채팅 보내기 요청에 대한 응답, 주변 섹터 유저에게 SendPacket할때 마다 카운팅)
	LONG                                m_ResChatMsgTPS; // 초당 메세지 응답 갯수(채팅 보내기 요청에 대한 응답, 주변 섹터 유저에게 SendPacket할때 마다 카운팅)


	///////////////////////////////////////////////////////////////////////////
    // ChatServer Config 변수
    ///////////////////////////////////////////////////////////////////////////
	INT                                 m_UserMaxCnt;
	INT                                 m_UpdateFrame;

public:
	ChatServer();
	~ChatServer();

	BOOL         RunServer();
	void         StopServer();

private:
	void         Mem_Init(INT userMAX, INT updateFrame, WCHAR* monitorserverip, INT monitorserverport);
	void         Thread_Create();
	void         Thread_Destroy();
		         
		         

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
	void SendPacket_SectorOne(CMessage* pMessage, WORD xpos, WORD ypos, CUser* pUser);
	void SendPakcet_SectorAround(CMessage* pMessage, CUser* pUser);
	void SectorFind(SECTOR_AROUND* pAround, WORD xpos, WORD ypos);
	BOOL SectorRemove(CUser* pUser);
	BOOL SectorRangeCheck(WORD xpos, WORD ypos);

	///////////////////////////////////////////////////////////////////////////
    // Recv Message 핸들러
    ///////////////////////////////////////////////////////////////////////////
	void LoginProc(CMessage* pMessage, UINT64 sessionid);
	void SectorMoveProc(CMessage* pMessage, UINT64 sessionid);
	void ChatMessageProc(CMessage* pMessage, UINT64 sessionid);
	void HeartBeatProc(CMessage* pMessage, UINT64 sessionid);

	///////////////////////////////////////////////////////////////////////////
    // Update Job 핸들러
    ///////////////////////////////////////////////////////////////////////////
	void JoinProc(UINT64 sessionID);
	void RecvProc(UINT64 sessionID, CMessage* pMessage);
	void DeleteProc(UINT64 sessionID);

	void UpdateThread();
	void MonitorThread();
};