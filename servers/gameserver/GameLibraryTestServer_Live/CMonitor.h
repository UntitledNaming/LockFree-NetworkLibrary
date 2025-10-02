#pragma once

class CGroup;
class ProcessMonitor;
class CMonitorClient;

class CMonitor : public CGroup
{
public:
	CMonitor();
	~CMonitor();

private:
	virtual void  OnClientJoin(UINT64 sessionID);
	virtual void  OnClientLeave(UINT64 sessionID);
	virtual void  OnRecv(UINT64 sessionID, CMessage* pMessage);
	virtual void  OnIUserMove(UINT64 sessioID, IUser* pUser);
	virtual void  OnUpdate();

private:
	ProcessMonitor* m_pPDH;
	CMonitorClient* m_pMonitorClient;
	time_t          m_start;
	tm*             m_local_time;

	INT64           m_loopCnt;
	INT64           m_SendIOSum;
	INT64           m_RecvIOSum;
	INT64           m_AcceptTPSSum;
	INT64           m_CMessagePoolSum;
	INT64           m_UserPoolSum;
	INT64           m_AuthRecvTPSSum;
	INT64           m_AuthSendTPSSum;
	INT64           m_AuthFrameTPSSum;
	INT64           m_EchoRecvTPSSum;
	INT64           m_EchoSendTPSSum;
	INT64           m_EchoFrameTPSSum;
	INT64           m_MonitorFrameTPSSum;

	float           m_Processtotalsum;
	float           m_Processusersum;
	float           m_Processkernelsum;
	double          m_Tcpretransmitsum;
	double          m_Tcpsegmentsentsum;
	double          m_Ethernet1sendsum;
	double          m_Ethernet2sendsum;
};

