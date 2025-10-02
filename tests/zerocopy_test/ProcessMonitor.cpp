#include <windows.h>
#include <stdio.h>
#include <Pdh.h>
#include <pdhmsg.h>
#include <string>
#include <vector>
#include <tlhelp32.h>
#pragma comment(lib,"Pdh.lib")
#include "CPUUsage.h"
#include "ProcessMonitor.h"

ProcessMonitor::ProcessMonitor(int threadCnt, const UINT* pthreadIDArray)
{
	PDH_STATUS status;
	WCHAR countPath[MAX_PATH];

	// ���μ��� �̸� ���
	std::wstring processName;
	if (!GetProcessName(processName))
		__debugbreak();

	m_threacCnt = threadCnt;
	m_processCS = 0;
	m_pProcessCSQry = nullptr;
	m_pProcessCSCnter = nullptr;
	m_pProcessCSVal = nullptr;

	//���� �ڵ� ����
    PdhOpenQuery(NULL, NULL, &m_processUserMemoryQry);
    PdhOpenQuery(NULL, NULL, &m_processNonPagedrMemoryQry);
    PdhOpenQuery(NULL, NULL, &m_AvailableMemoryQry);
    PdhOpenQuery(NULL, NULL, &m_NonPagedMemoryQry);
	PdhOpenQuery(NULL, NULL, &m_TCPReTransmitQry);
	PdhOpenQuery(NULL, NULL, &m_TCPSegmentSentQry);
	PdhOpenQuery(NULL, NULL, &m_EtherNetSendQry1);
	PdhOpenQuery(NULL, NULL, &m_EtherNetSendQry2);
	PdhOpenQuery(NULL, NULL, &m_EtherNetRecvQry1);
	PdhOpenQuery(NULL, NULL, &m_EtherNetRecvQry2);

	//ī���� ����
	swprintf(countPath, MAX_PATH, L"\\Process(%s)\\Private Bytes", processName.c_str());
	PdhAddCounter(m_processUserMemoryQry, countPath, NULL, &m_processUserMemoryCnter);
	memset(countPath, 0, MAX_PATH);

	swprintf(countPath, MAX_PATH, L"\\Process(%s)\\Pool Nonpaged Bytes", processName.c_str());
	PdhAddCounter(m_processNonPagedrMemoryQry, countPath, NULL, &m_processNonPagedMemoryCnter);
	memset(countPath, 0, MAX_PATH);

	PdhAddCounter(m_AvailableMemoryQry, L"\\Memory\\Available MBytes", NULL, &m_AvailableMemoryCnter);

	PdhAddCounter(m_NonPagedMemoryQry, L"\\Memory\\Pool Nonpaged MBytes", NULL, &m_NonPagedMemoryCnter);

	PdhAddCounter(m_TCPReTransmitQry, L"\\TCPv4\\Segments Retransmitted/sec", NULL, &m_TCPReTransmitCnter);

	PdhAddCounter(m_TCPSegmentSentQry, L"\\TCPv4\\Segments Sent/sec", NULL, &m_TCPSegmentSentCnter);



	// �����ڿ� ���� ����� �־��� ��� �����庰 cs ��� �۾� ó��
	if (pthreadIDArray != nullptr && threadCnt != 0)
	{
		std::wstring namebuf;

		m_pProcessCSQry = new PDH_HQUERY[threadCnt];
		m_pProcessCSCnter = new PDH_HCOUNTER[threadCnt];
		m_pProcessCSVal = new PDH_FMT_COUNTERVALUE[threadCnt];

		for (int i = 0; i < threadCnt; i++)
		{
			if (PdhOpenQuery(NULL, NULL, &m_pProcessCSQry[i]) != ERROR_SUCCESS)
			{
				__debugbreak();
			}

			if (!FindThreadInstanceNameByTID(processName, pthreadIDArray[i], namebuf))
				__debugbreak();

			swprintf(countPath, MAX_PATH, L"\\Thread(%s)\\Context Switches/sec",namebuf.c_str());
			status = PdhAddCounter(m_pProcessCSQry[i], countPath, 0, &m_pProcessCSCnter[i]);
			if (status != ERROR_SUCCESS)
			{
				wprintf(L"PdhAddCounter failed: 0x%08X\n", status);
				__debugbreak();
			}
			memset(countPath, 0, MAX_PATH);

		}

		

	}


}

ProcessMonitor::~ProcessMonitor()
{
	if (m_threacCnt != 0 && m_pProcessCSQry !=nullptr && m_pProcessCSCnter!= nullptr && m_pProcessCSVal != nullptr)
	{
		delete[] m_pProcessCSQry;
		delete[] m_pProcessCSCnter;
		delete[] m_pProcessCSVal;
	}
}

void ProcessMonitor::UpdateCounter()
{
	PDH_STATUS status;

	m_processCS = 0;

	//������ ����
	PdhCollectQueryData(m_processUserMemoryQry);
	PdhCollectQueryData(m_processNonPagedrMemoryQry);
	PdhCollectQueryData(m_AvailableMemoryQry);
	PdhCollectQueryData(m_NonPagedMemoryQry);
	PdhCollectQueryData(m_TCPReTransmitQry);
	PdhCollectQueryData(m_TCPSegmentSentQry);
	PdhCollectQueryData(m_EtherNetSendQry1);
	PdhCollectQueryData(m_EtherNetSendQry2);
	PdhCollectQueryData(m_EtherNetRecvQry1);
	PdhCollectQueryData(m_EtherNetRecvQry2);

	for (int i = 0; i < m_threacCnt; i++)
	{
		PdhCollectQueryData(m_pProcessCSQry[i]);
	}

	//���� ������ ���
	PdhGetFormattedCounterValue(m_processUserMemoryCnter, PDH_FMT_DOUBLE, NULL, &m_processUserMemoryVal);

	PdhGetFormattedCounterValue(m_processNonPagedMemoryCnter, PDH_FMT_DOUBLE, NULL, &m_processNonPagedMemoryVal);

	PdhGetFormattedCounterValue(m_AvailableMemoryCnter, PDH_FMT_DOUBLE, NULL, &m_AvailableMemoryVal);

	PdhGetFormattedCounterValue(m_NonPagedMemoryCnter, PDH_FMT_DOUBLE, NULL, &m_NonPagedMemoryVal);

	PdhGetFormattedCounterValue(m_TCPReTransmitCnter, PDH_FMT_DOUBLE, NULL, &m_TCPReTransmitVal);

	PdhGetFormattedCounterValue(m_TCPSegmentSentCnter, PDH_FMT_DOUBLE, NULL, &m_TCPSegmentSentVal);

	PdhGetFormattedCounterValue(m_EtherNetSendCnter1, PDH_FMT_DOUBLE, NULL, &m_EtherNetSendVal1);

	PdhGetFormattedCounterValue(m_EtherNetSendCnter2, PDH_FMT_DOUBLE, NULL, &m_EtherNetSendVal2);

	PdhGetFormattedCounterValue(m_EtherNetRecvCnter1, PDH_FMT_DOUBLE, NULL, &m_EtherNetRecvVal1);

	PdhGetFormattedCounterValue(m_EtherNetRecvCnter2, PDH_FMT_DOUBLE, NULL, &m_EtherNetRecvVal2);

	for (int i = 0; i < m_threacCnt; i++)
	{
		PdhGetFormattedCounterValue(m_pProcessCSCnter[i], PDH_FMT_DOUBLE, NULL, &m_pProcessCSVal[i]);
		m_processCS += m_pProcessCSVal[i].doubleValue;
	}




	//CPU��뷮 ����
	UpdateCpuTime();
}


// ���μ��� �̸� ����
bool ProcessMonitor::GetProcessName(std::wstring& outName)
{
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	std::wstring fullName;
	size_t pos;
	const std::wstring ext = L".exe";



	if (hSnap == INVALID_HANDLE_VALUE)
		return false;

	PROCESSENTRY32 pe = { sizeof(pe) };
	if (Process32First(hSnap, &pe)) 
	{
		do 
		{
			if (pe.th32ProcessID == GetCurrentProcessId()) 
			{
				fullName = pe.szExeFile;
				pos = fullName.rfind(ext);

				if (pos != std::wstring::npos && pos == fullName.length() - ext.length())
				{
					outName = fullName.substr(0, pos);
				}

				else
					outName = fullName;

				CloseHandle(hSnap);
				return true;
			}
		} while (Process32Next(hSnap, &pe));
	}

	CloseHandle(hSnap);
	return false;

}

bool ProcessMonitor::FindThreadInstanceNameByTID(const std::wstring& procName, DWORD tid, std::wstring& outInstName)
{
	DWORD nameBufSize = 0;
    DWORD counterBufSize = 0;
	PDH_STATUS status;

	status = PdhEnumObjectItemsW(NULL, NULL, L"Thread", NULL, &nameBufSize, NULL, &counterBufSize, PERF_DETAIL_NOVICE, 0);
	if (status != PDH_MORE_DATA)
	{
		wprintf(L"ù PdhEnumObjectItemsW ����: 0x%08x\n", status);
		return false;
	}

	std::vector<wchar_t> counterBuffer(nameBufSize);
	std::vector<wchar_t> instanceBuffer(counterBufSize);
	status = PdhEnumObjectItemsW(NULL, NULL, L"Thread", counterBuffer.data(), &nameBufSize, instanceBuffer.data(), &counterBufSize, PERF_DETAIL_NOVICE, 0);
	if (status == ERROR_SUCCESS)
	{
		// ���������� nameBuf ä��
	}
	else if (status == PDH_MORE_DATA)
	{

	}
	else
	{
		wprintf(L"2�� PdhEnumObjectItemsW ����: 0x%08X\n", status);
		return false;
	}

	wchar_t* p = instanceBuffer.data();

	// nameBuffer�� ����� ��� �ν��Ͻ� �̸��� Ž���ϱ�
	while (*p)
	{
		// nameBuffer�� ����� �ν��Ͻ� �̸� 1���� ����
		std::wstring instance = p;

		// �ν��Ͻ� �̸��� "procName/"�� �������� ������ ����
		if (!procName.empty())
		{
			if (instance.find(procName + L"/") != 0)
			{
				p += wcslen(p) + 1;
				continue;
			}
		}

		// Thread �ν��Ͻ��� "���μ�����/��ȣ" ����
		std::wstring counterPath = L"\\Thread(" + instance + L")\\ID Thread";

		PDH_HQUERY query;
		PDH_HCOUNTER counter;
		PDH_FMT_COUNTERVALUE value;

		if (PdhOpenQueryW(NULL, 0, &query) == ERROR_SUCCESS)
		{
			if (PdhAddCounterW(query, counterPath.c_str(), 0, &counter) == ERROR_SUCCESS)
			{
				PdhCollectQueryData(query);

				if (PdhGetFormattedCounterValue(counter, PDH_FMT_LONG, NULL, &value) == ERROR_SUCCESS)
				{
					if ((DWORD)value.longValue == tid)
					{
						outInstName = instance;
						PdhCloseQuery(query);
						return true;
					}
				}
			}
			PdhCloseQuery(query);
		}
		p += wcslen(p) + 1;
	}

	return false;
}


