#pragma once
#pragma comment(lib,"DbgHelp.lib")
#include <windows.h>
#include <crtdbg.h>
#include <DbgHelp.h>
#include <iostream>
#include <psapi.h>


class CCrashDump
{
public:
	static long _DumpCount;

	CCrashDump()
	{
		_DumpCount = 0;

		_invalid_parameter_handler oldHandler;
		_invalid_parameter_handler newHandler;

		newHandler = myInvalidParameterHandler;
		oldHandler = _set_invalid_parameter_handler(newHandler); //crt�Լ��� null ������ �־��� �� �߻�
		_CrtSetReportMode(_CRT_WARN, 0);                        //CRT ���� �޼��� ǥ�� ���ϰ� �ٷ� ���� �����
		_CrtSetReportMode(_CRT_ASSERT, 0);                      //CRT ���� �޼��� ǥ�� ���ϰ� �ٷ� ���� �����
		_CrtSetReportMode(_CRT_ERROR, 0);                       //CRT ���� �޼��� ǥ�� ���ϰ� �ٷ� ���� �����

		_CrtSetReportHook(_custom_Report_hook);

		//Pure virtual function called ������ ���� ������ �Լ��� ��ȸ�ϵ���
		_set_purecall_handler(myPurecallHandler);

		SetHandlerDump();

	}

	static void Crash()
	{
		int* p = nullptr;
		*p = 0;
	}

	static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
	{
		int iWorkingMemory = 0;
		SYSTEMTIME    stNowTime;


		long DumpCount = InterlockedIncrement(&_DumpCount);

		//���� ��¥�� �ð� ���
		WCHAR filename[MAX_PATH];

		GetLocalTime(&stNowTime);
		wsprintf(filename, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d.dmp", stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, DumpCount);
		wprintf(L"\n\n\n!!! Crash Error !!!  %d.%d.%d / %d:%d:%d \n", stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);
		wprintf(L"Now Save Dump File... \n");

		HANDLE hDumpFile = ::CreateFile(filename,
			GENERIC_WRITE,
			FILE_SHARE_WRITE,
			NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (hDumpFile != INVALID_HANDLE_VALUE)
		{
			_MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInformation;

			MinidumpExceptionInformation.ThreadId = ::GetCurrentThreadId();
			MinidumpExceptionInformation.ExceptionPointers = pExceptionPointer;
			MinidumpExceptionInformation.ClientPointers = TRUE;


			MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory,
				&MinidumpExceptionInformation, NULL, NULL);

			CloseHandle(hDumpFile);

			wprintf(L"CrashDump Save Finish");
		}


		//���� �� �������� CreateFile�� �����ϸ� 

		

		//else
		//{
		//	HANDLE hProcess = 0;
		//	SIZE_T memorySize;
		//	PROCESS_MEMORY_COUNTERS pmc;
		//	hProcess = GetCurrentProcess();


		//	if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
		//	{
		//		//������� �޸� ���ϱ�
		//		memorySize = (SIZE_T)(pmc.WorkingSetSize);

		//		//������� �޸� ũ�� ������ ������ �Ҵ� �� Ŀ��
		//		void* pAlloc = VirtualAlloc(NULL, memorySize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		//		
		//		if (pAlloc == nullptr)
		//		{
		//			printf("Virtual Alloc Error \n");
		//			return;
		//		}

		//		bool ret = MiniDumpWriteDump()
		//		MiniDumpWriteDump()
		//	}
		//	

		//}


		return EXCEPTION_EXECUTE_HANDLER;
	}

	static void SetHandlerDump()
	{
		SetUnhandledExceptionFilter(MyExceptionFilter);
	}

	//Invalid Parameter Handler
	static void myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
	{
		Crash();
	}

	static int _custom_Report_hook(int ireposttype, char* message, int* returnvalue)
	{
		Crash();
		return true;
	}

	static void myPurecallHandler()
	{
		Crash();
	}

};

long CCrashDump::_DumpCount = 0;