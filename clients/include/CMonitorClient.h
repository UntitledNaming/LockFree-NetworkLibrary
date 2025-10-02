#pragma once

class CMonitorClient : public CLanClient
{
public:
	CMonitorClient(INT serverno);
	~CMonitorClient();
	
	bool SendMonitorData(BYTE dataType, INT dataValue);

private:
	virtual void OnEnterJoinServer() override;               // 서버와 연결 성공 후       
	virtual void OnLeaveServer() override;                   // 서버와 연결 끊어진 후
	virtual void OnRecv(CMessage* pMessage) override;        // 패킷 수신 완료 후
	virtual void OnSend(int sendsize) override;              // 패킷 송신 완료 후

private:
	INT m_ServerNo;
};