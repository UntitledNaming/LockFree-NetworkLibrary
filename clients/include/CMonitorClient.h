#pragma once

class CMonitorClient : public CLanClient
{
public:
	CMonitorClient(INT serverno);
	~CMonitorClient();
	
	bool SendMonitorData(BYTE dataType, INT dataValue);

private:
	virtual void OnEnterJoinServer() override;               // ������ ���� ���� ��       
	virtual void OnLeaveServer() override;                   // ������ ���� ������ ��
	virtual void OnRecv(CMessage* pMessage) override;        // ��Ŷ ���� �Ϸ� ��
	virtual void OnSend(int sendsize) override;              // ��Ŷ �۽� �Ϸ� ��

private:
	INT m_ServerNo;
};