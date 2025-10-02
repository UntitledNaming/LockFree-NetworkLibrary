#include <windows.h>
#include <stdio.h>
#include "CPUUsage.h"



CCpuUsage::CCpuUsage(HANDLE hProcess)
{
	//---------------------------------------------------------------------
	// 프로세스 핸들 입력 없으면 자기 자신을 대상
	//---------------------------------------------------------------------
	if (hProcess == INVALID_HANDLE_VALUE)
	{
		m_hProcess = GetCurrentProcess();
	}

	//---------------------------------------------------------------------
	// 프로세서 갯수를 확인
	// 프로세스 실행률 계산시 CPU 갯수로 나누기를 하여 실제 사용률을 구함
	//---------------------------------------------------------------------
	SYSTEM_INFO SystemInfo;

	GetSystemInfo(&SystemInfo);
	m_iNumberOfProcessor = SystemInfo.dwNumberOfProcessors;


    m_fProcessorTotal = 0;
    m_fProcessorUser = 0;
    m_fProcessorKernel = 0;
   
    m_fProcessTotal = 0;
    m_fProcessUser = 0;
    m_fProcessKernel = 0;

	m_fProcessor_LastKernel.QuadPart = 0;
	m_fProcessor_LastUser.QuadPart = 0;
	m_fProcessor_LastIdle.QuadPart = 0;


	m_fProcess_LastKernel.QuadPart = 0;
	m_fProcess_LastUser.QuadPart = 0;
	m_fProcess_LastTime.QuadPart = 0;

	UpdateCpuTime();
}


//----------------------------------------------------------
// CPU 사용률을 갱신함. 500ms ~ 1000ms 단위의 호출이 적당
//----------------------------------------------------------
void CCpuUsage::UpdateCpuTime()
{
	//------------------------------------------------------------------------------
	// 프로세서 사용률을 갱신함.
	// 
	// 사용 구조체는 FILETIME 이지만, ULARGE_INTEGER와 구조가 같아서 이를 사용함
	// FILETIME 구조체는 100ns 단위의 시간 단위를 표현하는 구조체임.
	//-------------------------------------------------------------------------------

	_ULARGE_INTEGER Idle;
	_ULARGE_INTEGER Kernel;
	_ULARGE_INTEGER User;

	//------------------------------------------------------------------------------
	// 시스템 사용 시간을 구함
	// 
	// Idle 시간  / 커널 사용 시간(Idle 포함) / 유저 사용 타임 
	//------------------------------------------------------------------------------
	if (GetSystemTimes((PFILETIME)&Idle, (PFILETIME)&Kernel, (PFILETIME)&User) == false)
	{
		return;
	}

	// 커널 시간에는 Idle 포함되어 있음. 그래서 빼야 함.
	ULONGLONG KernelDiff = Kernel.QuadPart - m_fProcessor_LastKernel.QuadPart;
	ULONGLONG UserDiff = User.QuadPart - m_fProcessor_LastUser.QuadPart;
	ULONGLONG IdleDiff = Idle.QuadPart - m_fProcessor_LastIdle.QuadPart;

	ULONGLONG Total = KernelDiff + UserDiff;
	ULONGLONG TimeDiff;

	if (Total == 0)
	{
		m_fProcessorUser = 0.0f;
		m_fProcessorKernel = 0.0f;
		m_fProcessorTotal = 0.0f;
	}
	else
	{
		//커널 타임에 Idle 타임이 있으니 빼서 계산하기
		m_fProcessorTotal = (float)((double)(Total - IdleDiff) / Total * 100.0f);
		m_fProcessorUser = (float)((double)UserDiff / Total * 100.0f);
		m_fProcessorKernel = (float)((double)(KernelDiff - IdleDiff) / Total * 100.0f);

	}

	//갱신
	m_fProcessor_LastKernel = Kernel;
	m_fProcessor_LastUser = User;
	m_fProcessor_LastIdle = Idle;


	//--------------------------------------------------------------------------
	// 지정된 프로세스 사용률을 갱신함
	//--------------------------------------------------------------------------
	ULARGE_INTEGER None;
	ULARGE_INTEGER NowTime;


	//--------------------------------------------------------------------------
	// 현재 100ns 단위의 시간을 구함. UTC 시간
	// 
	// 프로세스 사용률 판단 공식
	// 
	// a = 샘플 간격의 시스템 시간(실제로 지나간 시간)
	// b = 프로세스의 CPU 사용 시간
	// 
	// a : 100 = b : 사용률
	// 
	// 위 공식으로 b인 프로세스의 CPU 사용 시간을 구함.
	// 
	//--------------------------------------------------------------------------

	//--------------------------------------------------------------------------
	// 얼마의 시간이 지났는지 100ns 시간을 구함(현재 시간 구하는 함수)
	//--------------------------------------------------------------------------
	GetSystemTimeAsFileTime((LPFILETIME)&NowTime); 

	
	//--------------------------------------------------------------------------
	// 해당 프로세스가 사용한 시간을 구함.
	// 
	// 두번째, 세번째는 실행, 종료 시간으로 미사용함.
	//--------------------------------------------------------------------------
	GetProcessTimes(m_hProcess, (LPFILETIME)&None, (LPFILETIME)&None, (LPFILETIME)&Kernel, (LPFILETIME)&User);

	//----------------------------------------------------------------------------------
	// 이전에 저장된 프로세스 시간과의 차를 구해서 실제로 얼마의 시간이 지났는지 확인
	//----------------------------------------------------------------------------------
	TimeDiff = NowTime.QuadPart - m_fProcess_LastTime.QuadPart;
	UserDiff = User.QuadPart - m_fProcess_LastUser.QuadPart;
	KernelDiff = Kernel.QuadPart - m_fProcess_LastKernel.QuadPart;

	Total = KernelDiff + UserDiff;


	m_fProcessTotal = (float)(Total / (double)m_iNumberOfProcessor / (double)TimeDiff * 100.0f);
	m_fProcessKernel = (float)(KernelDiff / (double)m_iNumberOfProcessor / (double)TimeDiff * 100.0f);
	m_fProcessUser = (float)(UserDiff / (double)m_iNumberOfProcessor / (double)TimeDiff * 100.0f);

	m_fProcess_LastTime = NowTime;
	m_fProcess_LastKernel = Kernel;
	m_fProcess_LastUser = User;
}
