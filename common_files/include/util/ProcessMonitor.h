#pragma once

typedef double DOUBLE;

class ProcessMonitor : public CCpuUsage
{

public:
	ProcessMonitor(int threadCnt = 0, const UINT* pthreadIDArray = nullptr);
	~ProcessMonitor();

	void   UpdateCounter();

private:
	bool   GetProcessName(std::wstring& outName);
	bool   FindThreadInstanceNameByTID(const std::wstring& procName, DWORD tid, std::wstring& outInstName);

public:
	// �����ڷ� ������ ������ ����
	UINT                  m_threacCnt;

	// ���μ��� ���ؽ�Ʈ ����Ī ��
	DOUBLE                m_processCS;

	//���μ��� ���� �Ҵ� �޸� ���� �� ī����, ��
	PDH_HQUERY            m_processUserMemoryQry;
	PDH_HCOUNTER          m_processUserMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_processUserMemoryVal;

	//���μ��� �������� �޸� ���� �� ī����, ��
	PDH_HQUERY            m_processNonPagedrMemoryQry;
	PDH_HCOUNTER          m_processNonPagedMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_processNonPagedMemoryVal;

	//��밡�� �޸� ���� �� ī����, ��
	PDH_HQUERY            m_AvailableMemoryQry;
	PDH_HCOUNTER          m_AvailableMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_AvailableMemoryVal;

	//���������� �޸� ���� �� ī����, ��
	PDH_HQUERY            m_NonPagedMemoryQry;
	PDH_HCOUNTER          m_NonPagedMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_NonPagedMemoryVal;

	// TCP �����۷�
	PDH_HQUERY            m_TCPReTransmitQry;
	PDH_HCOUNTER          m_TCPReTransmitCnter;
	PDH_FMT_COUNTERVALUE  m_TCPReTransmitVal;


	// TCP ���۷�
	PDH_HQUERY            m_TCPSegmentSentQry;
	PDH_HCOUNTER          m_TCPSegmentSentCnter;
	PDH_FMT_COUNTERVALUE  m_TCPSegmentSentVal;

	// �̴���1 Send ���۷�
	PDH_HQUERY            m_EtherNetSendQry1;
	PDH_HCOUNTER          m_EtherNetSendCnter1;
	PDH_FMT_COUNTERVALUE  m_EtherNetSendVal1;

	// �̴���2 Send ���۷�
	PDH_HQUERY            m_EtherNetSendQry2;
	PDH_HCOUNTER          m_EtherNetSendCnter2;
	PDH_FMT_COUNTERVALUE  m_EtherNetSendVal2;

	// �̴���1 Recv��
	PDH_HQUERY            m_EtherNetRecvQry1;
	PDH_HCOUNTER          m_EtherNetRecvCnter1;
	PDH_FMT_COUNTERVALUE  m_EtherNetRecvVal1;

	// �̴���2 Recv
	PDH_HQUERY            m_EtherNetRecvQry2;
	PDH_HCOUNTER          m_EtherNetRecvCnter2;
	PDH_FMT_COUNTERVALUE  m_EtherNetRecvVal2;

	// ���μ��� ��ü ���ؽ�Ʈ ����Ī Ƚ��(���߿� �����尡 ����Ǵ� ��Ȳ������ ��ȿ�� ���� ����)
	PDH_HQUERY*           m_pProcessCSQry;
	PDH_HCOUNTER*         m_pProcessCSCnter;
	PDH_FMT_COUNTERVALUE* m_pProcessCSVal;

};