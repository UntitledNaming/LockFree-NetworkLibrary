#pragma once

#define ID_MAX          20
#define NICK_MAX        20
#define SESSION_KEY_MAX 64
#define TIMEOUT1        3000
#define TIMEOUT2        10000

class DBTLS;
class CMonitorClient;
class ProcessMonitor;
class CUser;


template <typename T>
class CMemoryPool;

class CLoginServer : public CNetServer
{
private:

	struct st_ServerInfo
	{
		std::wstring  s_ServerIP;
		USHORT        s_GamePort;
		USHORT        s_ChatPort;
	}typedef SERVERINFO;

private:
	std::unordered_map<UINT64, CUser*>             m_UserMap;
	std::unordered_map<UINT64, DWORD>              m_NonUserMap;
	std::unordered_map<std::wstring, SERVERINFO*>  m_ServerInfoMap;     // key : 더미 or 실제 클라IP / value : 해당하는 서버 ip 및 port

	DBTLS*                                         m_DBTLS;
	CMemoryPool<CUser>*                            m_pUserPool;
	CMonitorClient*                                m_pMonitorClient;    // 모니터링 서버 접속 클라이언트
	BOOL                                           m_EndFlag;           // 모니터링 스레드 종료 플래그
	SRWLOCK                                        m_UserMapLock;
	SRWLOCK                                        m_NonUserMapLock;
	SRWLOCK                                        m_ServerInfoMapLock;


	cpp_redis::client*                             m_pRedisClient;

	std::thread                                    m_Frame;             // TimeOut 체크할 스레드
	std::thread                                    m_Monitor;       

	ProcessMonitor*                                m_pPDH;

	/*
	*   모니터링용 변수
	*/
	INT                                            m_LoginComTPS;
											       
	// Config 정보						   	       
	INT                                            m_MaxUserCnt;

public:
	CLoginServer();
	~CLoginServer();

	BOOL  RunServer();
	void  StopServer();

	void  Mem_Init(INT maxUserCnt, const CHAR* DBIp, INT DBPort, const CHAR* RedisIp, INT RedisPort, WCHAR* MonitorIp, INT MonitorPort);
	void  Thread_Create();
	void  Thread_Destroy();
	void  ServerRoutInfo_Init();

	virtual bool  OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort);
	virtual void  OnClientJoin(UINT64 SessionID);
	virtual void  OnClientLeave(UINT64 SessionID);
	virtual void  OnRecv(UINT64 SessionID, CMessage* pMessage);

	/*
	*     로그인 요청 및 응답 메세지 생성
	*/
	void  LoginRequest(CMessage* pMessage, UINT64 sessionid);

	void  FindServerInfo(WCHAR* keyIp, SERVERINFO** OutIP);

	void  GetDBData(WCHAR* id, WCHAR* nick, UINT64 accountNo); // id, nick (OutParam), accountNo(InParam)

	void  SetRedisToken(UINT64 accountNo, const std::string& token);

	////////////////////////////////////////////////////////////////////
	// 유저 맵, 논 유저 맵 동시에 Lock걸고 논유저 or 유저 삽입
	////////////////////////////////////////////////////////////////////
	void  UserInsert(UINT64 sessionID, CUser* pUser);


	//타임아웃 처리 
	void  NonUserTimeOut();
	void  UserTimeOut();

private:
	void  FrameThread();
	void  MonitorThread();
};
