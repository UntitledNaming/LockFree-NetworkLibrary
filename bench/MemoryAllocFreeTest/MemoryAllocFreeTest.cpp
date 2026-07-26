#define PROFILE

#include <windows.h>
#include <iostream>
#include <process.h>
#include <string.h>

#include "MemoryPool.h"
#include "LockFreeMemoryPoolLive.h"
#include "MemoryPoolTLS.h"
#include "ProfilerTLS.h"

struct TestStruct
{
	int a = 0;
	int b = 0;
	int c = 0;
	int d = 0;
};

unsigned __stdcall ThreadFunc(void* arg);
int threadCnt = 0;
int allocfreetype = 0;
int allocfreeCnt = 0;
int iterPerTest = 0;
int testCount = 0;

CMPoolTLS<TestStruct> g_tlsPool(100);
MemoryPool<TestStruct> g_srwlockPool(1000);

int main()
{

	ULONG info = 0;
	SIZE_T ret = 0;
	HeapQueryInformation(GetProcessHeap(), HeapCompatibilityInformation,
		&info, sizeof(info), &ret);

	CProfilerManager::GetInstance()->Init();
	HANDLE* threadArray = nullptr;
	

	std::cout << "Thread Count : ";
	std::cin >> threadCnt;

	std::cout << "Alloc/Free Type(0 : new/delete, 1: memoryPoolSRWLOCK, 2: memorypoolTLS) : ";
	std::cin >> allocfreetype;

	std::cout << "Alloc/Free Count : ";
	std::cin >> allocfreeCnt;

	std::cout << "Iter Per Test : ";
	std::cin >> iterPerTest;

	std::cout << "Test Count(sample) : ";
	std::cin >> testCount;

	threadArray = new HANDLE[threadCnt];

	for (int i = 0; i < threadCnt; i++)
	{
		threadArray[i] = (HANDLE)_beginthreadex(NULL, NULL, ThreadFunc, (void*)i, 0, nullptr);
	}


	WaitForMultipleObjects(threadCnt, threadArray, TRUE, INFINITE);

	const WCHAR* name = nullptr;
	if (allocfreetype == 0)
	{
		name = L"new_delete";
	}
	else if (allocfreetype == 1)
	{
		name = L"srwlock_pool";
	}
	else if(allocfreetype == 2)
	{
		name = L"tls_pool";
	}

	CProfilerManager::GetInstance()->ProfileDataOutText(const_cast<WCHAR*>(name));

	for (int i = 0; i < threadCnt; i++)
	{
		CloseHandle(threadArray[i]);
	}

	delete[] threadArray;
}

unsigned __stdcall ThreadFunc(void* arg)
{
	wprintf(L"TestThread Start : %d...\n", GetCurrentThreadId());
	int idx = (int)arg;

	if (threadCnt <= 6)
	{
		SetThreadAffinityMask(GetCurrentThread(), 1ULL << (idx * 2));
	}

	CProfilerManager::GetInstance()->ProfileInit();

	TestStruct** array = new TestStruct * [allocfreeCnt];

	if (allocfreetype == 0)
	{
		for (int k = 0; k < testCount; k++)
		{

			CProfiler pro(L"ThreadFunc");

			for (int i = 0; i < iterPerTest; i++)
			{
				for (int j = 0; j < allocfreeCnt; j++)
				{
					array[j] = new TestStruct;
				}

				for (int j = 0; j < allocfreeCnt; j++)
				{
					delete array[j];
				}
			}
		}
	}

	else if (allocfreetype == 1)
	{
		for (int k = 0; k < testCount; k++)
		{

			CProfiler pro(L"ThreadFunc");

			for (int i = 0; i < iterPerTest; i++)
			{
				for (int j = 0; j < allocfreeCnt; j++)
				{
					array[j] = g_srwlockPool.Alloc();
				}

				for (int j = 0; j < allocfreeCnt; j++)
				{
					g_srwlockPool.Free(array[j]);
				}
			}
		}

	}
	else if (allocfreetype == 2)
	{
		for (int k = 0; k < testCount; k++)
		{

			CProfiler pro(L"ThreadFunc");

			for (int i = 0; i < iterPerTest; i++)
			{
				for (int j = 0; j < allocfreeCnt; j++)
				{
					array[j] = g_tlsPool.Alloc();
				}

				for (int j = 0; j < allocfreeCnt; j++)
				{
					g_tlsPool.Free(array[j]);
				}
			}

		}
	}

	delete[] array;
	return 0;
}
