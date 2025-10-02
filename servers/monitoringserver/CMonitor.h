#pragma once
#define USER_TIMEOUT    30000
#define DB_UPDATE_TIME  600000
#define DB_HANDLE_COUNT 3

class CMonAgentsMgr;
class DBTLS;
class ProcessMonitor;

template <typename T>
class CMemoryPool;

template <typename T>
class LFQueue;

class CMonitor : public CLanServer
{
private:
	// DB 스레드 전달 위한 구조체 
	struct st_MONITOR
	{
		INT  s_timeStamp;
		INT  s_dataValue;
		BYTE s_serverNo;
		BYTE s_dataType;
	}typedef MONITOR_DATA;

	struct st_DB_DATA
	{
		INT64  s_timeStamp;
		INT64  s_dataTotal;
		INT    s_dataMin;
		INT    s_dataMax;
		INT    s_count;
		BYTE   s_serverNo;
	}typedef DB_DATA;

private:
	////////////////////////////////////////////////////////////////////////////
    // MornitorServer 자료 구조 및 멤버
    ////////////////////////////////////////////////////////////////////////////
	std::unordered_map<UINT64, CUser*>        m_UserMap;
	std::unordered_map<BYTE, DB_DATA*>        m_DataMap;
	std::thread                               m_DB;
	std::thread                               m_Monitor;
	std::thread                               m_Frame;                          // TimeOut 체크할 스레드
	CMemoryPool<CUser>*                       m_pUserPool;		                
	CMemoryPool<MONITOR_DATA>*                m_pMonitorPool;                   // DB 스레드로 던질 구조체 풀
	LFQueue<MONITOR_DATA*>*                   m_pDBQueue;
	DBTLS*                                    m_pDBTLS;
	CMonAgentsMgr*                            m_pAgentMgr;
	ProcessMonitor*                           m_pPDH;
	HANDLE                                    m_DBEvent[DB_HANDLE_COUNT];       // 0번 : 데이터 삽입 이벤트 , 1번 : DB 저장 이벤트 , 2번 : 스레드 종료 이벤트
	SRWLOCK                                   m_UserMapLock;
	BOOL                                      m_EndFlag;
public:
	CMonitor();
	~CMonitor();

	BOOL RunServer();
	void StopServer();


private:
	void          Mem_Init(const CHAR* DBip, INT DBport);

	///////////////////////////////////////////////////////////////////////////
    // Recv Message 핸들러
    ///////////////////////////////////////////////////////////////////////////
	void ServerLoginProc(UINT64 sessionID, CMessage* pMessage);
	void DataUpdateProc(UINT64 sessionID, CMessage* pMessage);

	void Thread_Create();
	void Thread_Destroy();

	void DataInsert(MONITOR_DATA* pData);
	void DBInsert();
	void DataEnqueue(INT timestamp, INT dataValue, BYTE serverno, BYTE datatype);

	void UserTimeOut();

	void FrameThread();
	void DBUpdate();
	void Monitor();

public:
	///////////////////////////////////////////////////////////////////////////
    // LanServer Callback 함수 구현
    ///////////////////////////////////////////////////////////////////////////
	virtual bool  OnConnectionRequest(WCHAR* InputIP, unsigned short InputPort) override;

	virtual void  OnClientJoin(UINT64 SessionID) override;

	virtual void  OnClientLeave(UINT64 SessionID) override;

	virtual void  OnRecv(UINT64 SessionID, CMessage* pMessage) override;

};