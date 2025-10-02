#pragma once

#define MAX_PATH 256 
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
	// 생성자로 전달한 스레드 갯수
	UINT                  m_threacCnt;

	// 프로세스 컨텍스트 스위칭 값
	DOUBLE                m_processCS;

	//프로세스 유저 할당 메모리 쿼리 및 카운터, 값
	PDH_HQUERY            m_processUserMemoryQry;
	PDH_HCOUNTER          m_processUserMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_processUserMemoryVal;

	//프로세스 논페이지 메모리 쿼리 및 카운터, 값
	PDH_HQUERY            m_processNonPagedrMemoryQry;
	PDH_HCOUNTER          m_processNonPagedMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_processNonPagedMemoryVal;

	//사용가능 메모리 쿼리 및 카운터, 값
	PDH_HQUERY            m_AvailableMemoryQry;
	PDH_HCOUNTER          m_AvailableMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_AvailableMemoryVal;

	//논페이지드 메모리 쿼리 및 카운터, 값
	PDH_HQUERY            m_NonPagedMemoryQry;
	PDH_HCOUNTER          m_NonPagedMemoryCnter;
	PDH_FMT_COUNTERVALUE  m_NonPagedMemoryVal;

	// TCP 재전송률
	PDH_HQUERY            m_TCPReTransmitQry;
	PDH_HCOUNTER          m_TCPReTransmitCnter;
	PDH_FMT_COUNTERVALUE  m_TCPReTransmitVal;


	// TCP 전송량
	PDH_HQUERY            m_TCPSegmentSentQry;
	PDH_HCOUNTER          m_TCPSegmentSentCnter;
	PDH_FMT_COUNTERVALUE  m_TCPSegmentSentVal;

	// 이더넷1 Send 전송량
	PDH_HQUERY            m_EtherNetSendQry1;
	PDH_HCOUNTER          m_EtherNetSendCnter1;
	PDH_FMT_COUNTERVALUE  m_EtherNetSendVal1;

	// 이더넷2 Send 전송량
	PDH_HQUERY            m_EtherNetSendQry2;
	PDH_HCOUNTER          m_EtherNetSendCnter2;
	PDH_FMT_COUNTERVALUE  m_EtherNetSendVal2;

	// 이더넷1 Recv량
	PDH_HQUERY            m_EtherNetRecvQry1;
	PDH_HCOUNTER          m_EtherNetRecvCnter1;
	PDH_FMT_COUNTERVALUE  m_EtherNetRecvVal1;

	// 이더넷2 Recv
	PDH_HQUERY            m_EtherNetRecvQry2;
	PDH_HCOUNTER          m_EtherNetRecvCnter2;
	PDH_FMT_COUNTERVALUE  m_EtherNetRecvVal2;

	// 프로세스 전체 컨텍스트 스위칭 횟수(도중에 스레드가 종료되는 상황에서는 유효성 보장 못함)
	PDH_HQUERY*           m_pProcessCSQry;
	PDH_HCOUNTER*         m_pProcessCSCnter;
	PDH_FMT_COUNTERVALUE* m_pProcessCSVal;

};