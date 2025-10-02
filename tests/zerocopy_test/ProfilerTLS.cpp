#include <windows.h>
#include <unordered_map>
#include <string>
#include <strsafe.h>
#pragma warning(disable:4996)
#pragma comment(lib,"winmm.lib")
#define PROFILE
#include "ProfilerTLS.h"

CProfileTLS::CProfileTLS()
{
}

CProfileTLS::~CProfileTLS()
{
}

bool CProfileTLS::Init()
{
	//TlsIndex ����
	m_tlsIdx = TlsAlloc();
	if (m_tlsIdx == TLS_OUT_OF_INDEXES)
	{
		wprintf(L"CProfilerManger_Init()_TlsAlloc Error : %d \n", GetLastError());
		return false;
	}

	//����ȭ ��ü �ʱ�ȭ
	InitializeSRWLock(&m_arrayLock);

	m_arrayIdx = 0;

	return true;
}

void CProfileTLS::Destroy()
{
	for (int i = 0; i < m_arrayIdx; i++)
	{
		delete m_sampleArray[i].s_tablePtr;
		m_sampleArray[i].s_tablePtr = nullptr;
	}
}

bool CProfileTLS::ProfileInit()
{
	AcquireSRWLockExclusive(&m_arrayLock);

	if (m_arrayIdx >= MAX_THREAD_COUNT)
	{
		wprintf(L"ProfilerManager_ProfileInit()_Thread Count Max Error \n");
		ReleaseSRWLockExclusive(&m_arrayLock);
		return false;
	}

	m_sampleArray[m_arrayIdx].s_threadID = GetCurrentThreadId();
	m_sampleArray[m_arrayIdx].s_tablePtr = new std::unordered_map<std::wstring, st_PROFILE_DATA>;

	if (!TlsSetValue(m_tlsIdx, m_sampleArray[m_arrayIdx].s_tablePtr))
	{
		wprintf(L"ProfilerManager_ProfileInit()_TlsSetValue Error : %d \n", GetLastError());
		ReleaseSRWLockExclusive(&m_arrayLock);
		return false;
	}

	m_arrayIdx++;
	ReleaseSRWLockExclusive(&m_arrayLock);

	return true;
}

void CProfileTLS::SaveProfilingData(WCHAR* szName)
{
	errno_t err;
	FILE* fp;
	SYSTEMTIME    stNowTime;


	//���� ��¥�� �ð� ���
	WCHAR filename[MAX_PATH];
	GetLocalTime(&stNowTime);
	wsprintf(filename, L"Profiling [%s]_%d%02d%02d_%02d.%02d.%02d.txt", szName, stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);

	//=======================================================================================================
	// 
	//�� ������ TLS�� ����� ���ø� �ڷᱸ�� ��ȸ�ϸ鼭 Tagging ������ Copy
	// 
	//=======================================================================================================
	PROFILEDATA*     ptr[MAX_THREAD_COUNT]; // ���� : �迭�� ù �ּ�
	size_t           size;
	INT              j = 0;
	for (int i = 0; i < m_arrayIdx; i++)
	{
		size = m_sampleArray[i].s_tablePtr->size();

		//�ؽ� ���̺� size ��ŭ st_PROFILE_DATA �迭 �����ؼ� �� �����͸� ptr[i]�� ����
		ptr[i] = new PROFILEDATA[size];

		std::unordered_map<std::wstring, PROFILEDATA>::iterator it;
		j = 0;

		//�� �������� �ؽ� ���̺��� iterator�� ��ȸ�ϸ鼭 ����� value�� ptr[i][j]�� �ִ� st_PROFILE_DATA ���ҿ� Copy
		for (it = m_sampleArray[i].s_tablePtr->begin(); it != m_sampleArray[i].s_tablePtr->end(); ++it)
		{
			ptr[i][j].s_callCnt = it->second.s_callCnt;
			ptr[i][j].s_totalTime = it->second.s_totalTime;
			ptr[i][j].s_name = it->second.s_name;

			for (int k = 0; k < 2; k++)
			{
				ptr[i][j].s_maxTime[k] = it->second.s_maxTime[k];
				ptr[i][j].s_minTime[k] = it->second.s_minTime[k];
			}

			j++;
		}
	}

	//=======================================================================================================
	// 
	//�� ������ �� ���ø� ������ ��� �� ���, ���� ���
	// 
	//=======================================================================================================


	const WCHAR* pHeader[3];
	for (int i = 0; i < 3; i++)
	{
		if (i == 1)
		{
			pHeader[i] = L"                                     Name |                     Average  |                            Min  |                            Max  |       Call  |\n";
			continue;
		}
		pHeader[i] = L"--------------------------------------------------------------------------------------------------------------------------------------------------------------\n";
	}

	//�� ������ �ؽ� ���̺� size �ջ�
	size_t totalsize = 0;
	size_t maxsize = 0;
	for (int i = 0; i < m_arrayIdx; i++)
	{
		maxsize = max(maxsize, m_sampleArray[i].s_tablePtr->size());
		totalsize += m_sampleArray[i].s_tablePtr->size();
	}


	//Total �ջ��� ���� �迭 ���� �� Tag �ʱ�ȭ �۾�
	st_TOTAL_DATA* pTotal = new st_TOTAL_DATA[totalsize];
	for (int i = 0; i < totalsize; i++)
	{
		pTotal[i].s_useFlag = 0;
		pTotal[i].s_callCnt = 0;
	}


	// ���� ������ ���Ͽ� ������ ���ۿ� Copy(����� �̶� ����)
	// �������Ϸ� ��� �� ������ ����
	// MAX_STRING_SIZE : ���Ͽ� ������ ������ ���ڿ� ����� 1�� �ִ� ũ��
	size_t reMain = (MAX_HEADER_SIZE + (MAX_STRING_SIZE) * (totalsize + maxsize + m_arrayIdx)) * sizeof(WCHAR);
	WCHAR* pDataBuffer = (WCHAR*)malloc(reMain);
	WCHAR* pEnd = nullptr;
	HRESULT ret;

	memset(pDataBuffer, NULL, reMain);

	// 1. ��� ����
	//3��° ���� : ���ڿ� Copy�� ���ڿ� �� NULL ����Ű�� ������
	//4��° ���� : Dest ���ۿ� ���ڿ� Copy�� ���� ũ��
	ret = StringCchPrintfExW(pDataBuffer, reMain, &pEnd, &reMain, 0, pHeader[0]);
	if (ret != S_OK)
	{
		__debugbreak();
	}

	for (int i = 1; i < 3; i++)
	{
		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, pHeader[i]);
		if (ret != S_OK)
		{
			__debugbreak();
		}

	}


	// 2. ������ ��� �� ���ۿ� ����
	size_t tableSize = 0;
	DOUBLE avgTime;
	DOUBLE maxTime;
	DOUBLE minTime;
	DWORD  threadID;
	DWORD  realSize = 0;
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);

	//������ �� ���ø� ������ ���ۿ� Copy
	for (int i = 0; i < m_arrayIdx; i++)
	{
		//�ؽ� ���̺� �� ������ �迭 ��ȸ
		tableSize = m_sampleArray[i].s_tablePtr->size();
		threadID = m_sampleArray[i].s_threadID;

		for (int j = 0; j < tableSize; j++)
		{
			//��ü TotalTick���� Max, Min �� ���� call Ƚ���� ����
			maxTime = static_cast<DOUBLE>((ptr[i][j].s_maxTime[0] + ptr[i][j].s_maxTime[1])) / 2 * 1000000 / freq.QuadPart;
			minTime = static_cast<DOUBLE>((ptr[i][j].s_minTime[0] + ptr[i][j].s_minTime[1])) / 2 * 1000000 / freq.QuadPart;
			if (ptr[i][j].s_callCnt <= 4)
			{
				avgTime = static_cast<DOUBLE>(ptr[i][j].s_totalTime / ptr[i][j].s_callCnt) * 1000000 / freq.QuadPart;
			}
			else
				avgTime = ((static_cast<DOUBLE>((ptr[i][j].s_totalTime - ptr[i][j].s_maxTime[0] - ptr[i][j].s_maxTime[1] - ptr[i][j].s_minTime[0] - ptr[i][j].s_minTime[1])) / (ptr[i][j].s_callCnt - 4)) * (double)1000000 / freq.QuadPart);

			ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, L"Thread ID : %d |         %s |          %4lf �� |          %4lf �� |           %4lf �� |      %lld | \n", threadID, ptr[i][j].s_name.c_str(), avgTime, minTime, maxTime, ptr[i][j].s_callCnt);
			if (ret != S_OK)
			{
				__debugbreak();
			}

			//Total�迭 ��ȸ �ϸ鼭 ���� Tag������ �����ֱ�
			int k;
			for (k = 0; k < totalsize; k++)
			{
				//���� Tag�� ������
				if (pTotal[k].s_name == ptr[i][j].s_name)
				{
					pTotal[k].s_totalCallTime += ptr[i][j].s_callCnt;
					pTotal[k].s_totalAvgTime += avgTime;
					pTotal[k].s_totalMaxTime += maxTime;
					pTotal[k].s_totalMinTime += minTime;
					pTotal[k].s_useFlag = 1;
					pTotal[k].s_callCnt++;
					break;
				}

			}


			//���� ptr[i][j]�� ����� st_PROFILE_DATA�� Tag�� ������ Total�迭 �ݺ��� �� ���Ƶ� ���� Tag�� ��ã������ k�� totalsize���� ���̰� total �迭�� ���� �Ҵ��ؼ� ������ ��������� ��.
			if (k == totalsize)
			{
				//�ٽ� Total �迭 ���鼭 �����ϴ� ���� ã��
				for (int l = 0; l < totalsize; l++)
				{
					//��� ���ϴ� �����̶�� 
					if (pTotal[l].s_useFlag == 0)
					{
						pTotal[l].s_name = ptr[i][j].s_name;
						pTotal[l].s_totalCallTime = ptr[i][j].s_callCnt;
						pTotal[l].s_totalAvgTime = avgTime;
						pTotal[l].s_totalMaxTime = maxTime;
						pTotal[l].s_totalMinTime = minTime;
						pTotal[l].s_useFlag = 1;
						pTotal[l].s_callCnt = 1;
						break;
					}
				}

			}

		}

		//�� ������ ���� ������ Copy������ ----- �����
		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, L"--------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
		if (ret != S_OK)
		{
			__debugbreak();
		}
	}

	//Total �迭�� �ִ� �� ����ϱ�
	INT cnt;
	for (int i = 0; i < totalsize; i++)
	{
		//�����ϴ� �迭�̸� Pass
		if (pTotal[i].s_useFlag == 0)
			continue;

		cnt = pTotal[i].s_callCnt;

		ret = StringCchPrintfExW(pEnd, reMain, &pEnd, &reMain, 0, L"         Total Data   |         %s |          %4lf �� |          %4lf �� |           %4lf �� |      %lld | \n", pTotal[i].s_name.c_str(), pTotal[i].s_totalAvgTime / cnt, pTotal[i].s_totalMinTime / cnt, pTotal[i].s_totalMaxTime / cnt, pTotal[i].s_totalCallTime);
		if (ret != S_OK)
		{
			__debugbreak();
		}
	}



	// 3. ���� ��Ʈ�� ����
	err = _wfopen_s(&fp, filename, L"wb");
	if (err != 0)
	{
		wprintf(L"���� ���� ����. ���� �ڵ�: %d \n", err);
		__debugbreak();
		return;
	}


	//����
	fwrite(pDataBuffer, (MAX_HEADER_SIZE + (MAX_STRING_SIZE) * (totalsize + maxsize + m_arrayIdx))*sizeof(WCHAR), 1, fp);


	// 4. ��Ʈ�� �ݱ� �� �� ����
	fclose(fp);

	for (int i = 0; i < m_arrayIdx; i++)
	{
		delete[] ptr[i];
		ptr[i] = nullptr;
	}
	delete[] pTotal;
	pTotal = nullptr;

	free(pDataBuffer);
	pDataBuffer = nullptr;
}

CProfileTLS::CProfiler::CProfiler(const WCHAR* tag)
{
	PROFILE_MAP_PTR ret;
	ret = (PROFILE_MAP_PTR)TlsGetValue(CProfileTLS::GetInstance()->m_tlsIdx);
	if (ret == nullptr)
	{
		if (!CProfileTLS::GetInstance()->ProfileInit())
			__debugbreak();
	}


	m_tag = const_cast<WCHAR*>(tag);
	PRO_BEGIN(const_cast<WCHAR*>(tag));
}

CProfileTLS::CProfiler::~CProfiler()
{
	PRO_END(const_cast<WCHAR*>(m_tag));
}

void CProfileTLS::CProfiler::ProfileBegin(WCHAR* szName)
{
	std::wstring str = szName;


	if (str.size() >= MAX_STRING_SIZE)
		__debugbreak();

	//������ ���ø� ������ �ڷᱸ���� ����
	PROFILE_MAP_PTR pTable;
	pTable = (PROFILE_MAP_PTR)TlsGetValue(CProfileTLS::GetInstance()->GetTlsIndex());

	std::unordered_map<std::wstring, st_PROFILE_DATA>::iterator it = pTable->find(str);


	// wstring str�� key�� �Ҷ� �ؽ� ���̺� �ش� key�� ���� ��� �ʱⰪ ���� �� ���̺� ����
	if (it == pTable->end())
	{
		m_sampleData.s_callCnt = 0;
		m_sampleData.s_totalTime = 0;
		m_sampleData.s_errorFlag = 0;
		m_sampleData.s_name = str;

		for (int i = 0; i < 2; i++)
		{
			m_sampleData.s_maxTime[i] = 0;
			m_sampleData.s_minTime[i] = ULLONG_MAX;
		}

	}

	else
	{
		//�ؽ� ���̺� ������ �� ������ SampleData ����
		m_sampleData.s_callCnt = it->second.s_callCnt;
		m_sampleData.s_errorFlag = it->second.s_errorFlag;
		m_sampleData.s_totalTime = it->second.s_totalTime;
		m_sampleData.s_name = it->second.s_name;

		for (int i = 0; i < 2; i++)
		{
			m_sampleData.s_maxTime[i] = it->second.s_maxTime[i];
			m_sampleData.s_minTime[i] = it->second.s_minTime[i];
		}
	}


	//ErrorFlag�� 0�̰ų� End���� ������ �÷��� ���� 0x0010�� �ƴϸ� Error�� �߻���Ŵ
	if (m_sampleData.s_errorFlag != 0 && m_sampleData.s_errorFlag != 0x0010)
		throw szName;


	//���������� ���� �Ǿ����� Begin ����� ������ ErrorFlag ����
	m_sampleData.s_errorFlag = 0xFFFF;

	m_threadID = GetCurrentThreadId();

	//�ð� ���� ����
	QueryPerformanceCounter(&m_sampleData.s_startTick);
}

void CProfileTLS::CProfiler::ProfileEnd(WCHAR* szName)
{
	//���� �ð� ����
	LARGE_INTEGER endTick;
	LARGE_INTEGER freq;
	uint64_t DiffTick;
	DWORD id;

	QueryPerformanceCounter(&endTick);
	QueryPerformanceFrequency(&freq);
	id = GetCurrentThreadId();
	if (m_threadID != id)
		__debugbreak();

	//�����ڿ��� ������ ��� ���� ���� �÷��׶� �ٸ��� ����
	if (m_sampleData.s_errorFlag != 0xFFFF)
		throw szName;

	//Total�� �ջ�
	DiffTick = (endTick.QuadPart - m_sampleData.s_startTick.QuadPart);
	m_sampleData.s_totalTime += DiffTick;

	//MAX�� ����
	if (m_sampleData.s_maxTime[0] < DiffTick)
	{
		uint64_t temp = m_sampleData.s_maxTime[0];
		m_sampleData.s_maxTime[0] = DiffTick;
		m_sampleData.s_maxTime[1] = temp;
	}

	//MIN�� ����
	if (m_sampleData.s_minTime[0] > DiffTick)
	{
		uint64_t temp = m_sampleData.s_minTime[0];
		m_sampleData.s_minTime[0] = DiffTick;
		m_sampleData.s_minTime[1] = temp;
	}

	//ȣ�ⷮ ����
	m_sampleData.s_callCnt++;

	//���� �÷��� ����
	m_sampleData.s_errorFlag = 0x0010;


	//��� ������ ����� �� ���� �� �ؽ� ���̺� ����
	PROFILE_MAP_PTR pTable;
	pTable = (PROFILE_MAP_PTR)TlsGetValue(CProfileTLS::GetInstance()->GetTlsIndex());

	std::unordered_map<std::wstring, st_PROFILE_DATA>::iterator it = pTable->find(m_sampleData.s_name);

	//���ο� Tag�϶�
	if (it == pTable->end())
	{
		pTable->insert(std::pair<std::wstring, st_PROFILE_DATA>(m_sampleData.s_name, m_sampleData));
		return;
	}

	//�̹� ������
	it->second = m_sampleData;
	return;
}
