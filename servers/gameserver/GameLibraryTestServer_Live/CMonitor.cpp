#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <timeapi.h>
#include <unordered_map>
#include <string>
#include <Pdh.h>
#include <codecvt>
#include <thread>

#include "TextParser.h"
#include "CommonProtocol.h"
#include "CPUUsage.h"
#include "ProcessMonitor.h"
#include "LogClass.h"
#include "MemoryPoolTLS.h"
#include "CMessage.h"
#include "IUser.h"
#include "CUser.h"

#include "LFQSingleLive.h"
#include "CGameLibrary.h"
#include "CGroup.h"
#include "CAuth.h"
#include "CEcho.h"
#include "Ring_Buffer.h"
#include "LFQMultiLive.h"
#include "CLanClient.h"
#include "CMonitorClient.h"
#include "CMonitor.h"

#pragma warning(disable:4996)

CMonitor::CMonitor()
{
	m_pPDH = new ProcessMonitor;
	m_pMonitorClient = new CMonitorClient(GAME_SERVER_NO);
	m_loopCnt = 1;
	m_SendIOSum = 0;
	m_RecvIOSum = 0;
	m_AcceptTPSSum = 0;
	m_CMessagePoolSum = 0;
	m_UserPoolSum = 0;
	m_AuthRecvTPSSum = 0;
	m_AuthSendTPSSum = 0;
	m_AuthFrameTPSSum= 0;
	m_EchoRecvTPSSum = 0;
	m_EchoSendTPSSum = 0;
	m_EchoFrameTPSSum= 0;
	m_MonitorFrameTPSSum;
	m_Processtotalsum = 0;
	m_Processusersum = 0;
	m_Processkernelsum = 0;
	m_Tcpretransmitsum = 0;
	m_Tcpsegmentsentsum = 0;
	m_Ethernet1sendsum = 0;
	m_Ethernet2sendsum = 0;
	m_start = time(NULL);
	m_local_time = localtime(&m_start);

	Parser parser;
	std::wstring monitorstr;
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

	if (!parser.LoadFile("CMonitorConfig.txt"))
		__debugbreak();

	Parser::st_Msg monitor;
	parser.GetValue("MONITOR_IP", &monitor);

	INT monitorport;
	parser.GetValue("MONITOR_PORT", &monitorport);

	monitorstr = converter.from_bytes(monitor.s_ptr);

	// 모니터링 서버 연결
	if (!m_pMonitorClient->Connect((WCHAR*)monitorstr.c_str(), monitorport))
	{
		__debugbreak();
	}
}

CMonitor::~CMonitor()
{
	m_pMonitorClient->Destroy();
	delete m_pPDH;
	delete m_pMonitorClient;
}

void CMonitor::OnClientJoin(UINT64 sessionID)
{
}

void CMonitor::OnClientLeave(UINT64 sessionID)
{
}

void CMonitor::OnRecv(UINT64 sessionID, CMessage* pMessage)
{
}

void CMonitor::OnIUserMove(UINT64 sessioID, IUser* pUser)
{
}

void CMonitor::OnUpdate()
{
	DWORD starttime = timeGetTime();
	CEcho* echoptr = (CEcho*)GetGroupPtr(L"Echo");
	CAuth* authptr = (CAuth*)GetGroupPtr(L"Auth");

	m_pPDH->UpdateCounter();

	// 변수 수집
	m_Processtotalsum += m_pPDH->ProcessTotal();
	m_Processusersum += m_pPDH->ProcessUser();
	m_Processkernelsum += m_pPDH->ProcessKernel();
	m_Tcpretransmitsum += m_pPDH->m_TCPReTransmitVal.doubleValue;
	m_Tcpsegmentsentsum += m_pPDH->m_TCPSegmentSentVal.doubleValue;
	m_Ethernet1sendsum += m_pPDH->m_EtherNetSendVal1.doubleValue;
	m_Ethernet2sendsum += m_pPDH->m_EtherNetSendVal2.doubleValue;
	m_CMessagePoolSum += CMessage::m_pMessagePool->GetUseCnt();
	m_UserPoolSum += CUser::m_pUserPool->GetUseCnt();
	m_AcceptTPSSum += GetAcceptTPS();
	m_RecvIOSum += GetRecvIOTPS();
	m_SendIOSum += GetSendIOTPS();

	m_AuthRecvTPSSum += authptr->GetRecvTPS();
	m_AuthSendTPSSum += authptr->GetSendTPS();
	m_AuthFrameTPSSum += authptr->GetFrameTPS();

	m_EchoRecvTPSSum += echoptr->GetRecvTPS();
	m_EchoSendTPSSum += echoptr->GetSendTPS();
	m_EchoFrameTPSSum += echoptr->GetFrameTPS();

	m_MonitorFrameTPSSum += GetFrameTPS();


	if (m_pMonitorClient->ConnectAlive())
	{
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SERVER_RUN, 1);
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SERVER_CPU, (int)m_pPDH->ProcessTotal());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SESSION, GetCurSessionCount());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_AUTH_PLAYER, authptr->GetUserCount());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_GAME_PLAYER, echoptr->GetUserCount());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_ACCEPT_TPS, GetAcceptTPS());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_PACKET_RECV_TPS, authptr->GetRecvTPS() + echoptr->GetRecvTPS());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_PACKET_SEND_TPS, authptr->GetSendTPS() + echoptr->GetSendTPS());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_AUTH_THREAD_FPS, authptr->GetFrameTPS());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_GAME_THREAD_FPS, echoptr->GetFrameTPS());
		m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_PACKET_POOL, CMessage::m_pMessagePool->GetUseCnt());

	}
	else
	{
		if (m_pMonitorClient->ReConnect())
		{
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SERVER_RUN, 1);
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SERVER_CPU, (int)m_pPDH->ProcessTotal());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SERVER_MEM, (int)(m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024)));
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_SESSION, GetCurSessionCount());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_AUTH_PLAYER, authptr->GetUserCount());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_GAME_PLAYER, echoptr->GetUserCount());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_ACCEPT_TPS, GetAcceptTPS());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_PACKET_RECV_TPS, authptr->GetRecvTPS() + echoptr->GetRecvTPS());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_PACKET_SEND_TPS, authptr->GetSendTPS() + echoptr->GetSendTPS());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_AUTH_THREAD_FPS, authptr->GetFrameTPS());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_GAME_THREAD_FPS, echoptr->GetFrameTPS());
			m_pMonitorClient->SendMonitorData(dfMONITOR_DATA_TYPE_GAME_PACKET_POOL, CMessage::m_pMessagePool->GetUseCnt());
		}
	}

	
	wprintf(L"Start Time : %04d / %02d / %02d, %02d:%02d:%02d\n",
		m_local_time->tm_year + 1900,
		m_local_time->tm_mon + 1,
		m_local_time->tm_mday,
		m_local_time->tm_hour,
		m_local_time->tm_min,
		m_local_time->tm_sec);
	wprintf(L"======================= TPS 모니터링 ================================\n");
	wprintf(L"Accept                                        TPS    : (Avg %lld ,%d) \n", m_AcceptTPSSum / m_loopCnt, GetAcceptTPS());
	wprintf(L"SendIOComplete                                TPS    : (Avg %lld, %d) \n", m_SendIOSum / m_loopCnt, GetSendIOTPS());
	wprintf(L"RecvIOComplete                                TPS    : (Avg %lld, %d) \n", m_RecvIOSum / m_loopCnt, GetRecvIOTPS());
	wprintf(L"Auth    Recv                                  TPS    : (Avg %lld, %d) \n", m_AuthRecvTPSSum / m_loopCnt, authptr->GetRecvTPS());
	wprintf(L"Auth    Send                                  TPS    : (Avg %lld, %d) \n", m_AuthSendTPSSum / m_loopCnt, authptr->GetSendTPS());
	wprintf(L"Auth    Frame                                 TPS    : (Avg %lld, %d) \n", m_AuthFrameTPSSum / m_loopCnt, authptr->GetFrameTPS());
	wprintf(L"Echo    Recv                                  TPS    : (Avg %lld, %d) \n", m_EchoRecvTPSSum / m_loopCnt, echoptr->GetRecvTPS());
	wprintf(L"Echo    Send                                  TPS    : (Avg %lld, %d) \n", m_EchoSendTPSSum / m_loopCnt, echoptr->GetSendTPS());
	wprintf(L"Echo    Frame                                 TPS    : (Avg %lld, %d) \n", m_EchoFrameTPSSum / m_loopCnt, echoptr->GetFrameTPS());
	wprintf(L"Monitor Frame                                 TPS    : (Avg %lld, %d) \n", m_MonitorFrameTPSSum / m_loopCnt, GetFrameTPS());

	wprintf(L"====================== 모니터링 프레임 ==============================\n");
	wprintf(L"Frame Time                              Time   : %dms \n", timeGetTime() - starttime);

	wprintf(L"====================== 카운트 모니터링 ==============================\n");
	wprintf(L"SessionTable                           Count   : %d \n", GetCurSessionCount());
	wprintf(L"Echo User                              Count   : %lld \n", echoptr->GetUserCount());
	wprintf(L"Auth User                              Count   : %lld \n", authptr->GetUserCount());
	wprintf(L"Accept  Total                          Count   : %lld \n", GetAcceptTotal());

	wprintf(L"====================== 사용량 모니터링 ==============================\n");
	wprintf(L" CMessagePool           Avg  UseCnt : %lld  / Count : %d \n", m_CMessagePoolSum / m_loopCnt, CMessage::m_pMessagePool->GetUseCnt());
	wprintf(L"     UserPool           Avg  UseCnt : %lld  / Count : %d \n", m_UserPoolSum / m_loopCnt, CUser::m_pUserPool->GetUseCnt());


	wprintf(L"[ CPU Usage : T[%f%] U[%f%] K[%f%]]\n", m_Processtotalsum / m_loopCnt, m_Processusersum / m_loopCnt, m_Processkernelsum / m_loopCnt);
	wprintf(L"[ Available        Memory Usage : %lf MByte ] [ NonPagedMemory Usage : %lf MByte ]\n", m_pPDH->m_AvailableMemoryVal.doubleValue / (1024 * 1024), m_pPDH->m_NonPagedMemoryVal.doubleValue / (1024*1024));
	wprintf(L"[ Process User     Memory Usage : %lf MByte ]  [ Process NonPaged Memory Usage : %lf KByte ]\n", m_pPDH->m_processUserMemoryVal.doubleValue / (1024 * 1024), m_pPDH->m_processNonPagedMemoryVal.doubleValue / 1024);
	wprintf(L"[ TCP Retransmitted Avg   Count : %lf /sec  ]  [ TCP Segment Sent  Avg   Count : % lf / sec]\n", m_Tcpretransmitsum / m_loopCnt, m_Tcpsegmentsentsum / m_loopCnt);
	wprintf(L"[ Ethernet Send     Avg   Byte  : %lf KByte/sec]\n", ((m_Ethernet1sendsum + m_Ethernet1sendsum) / (1024)) / m_loopCnt);


	SetAcceptTPS(0);
	SetRecvIOTPS(0);
	SetSendIOTPS(0);
	SetFrameTPS(0);
	authptr->SetRecvTPS(0);
	authptr->SetSendTPS(0);
	authptr->SetFrameTPS(0);
	echoptr->SetRecvTPS(0);
	echoptr->SetSendTPS(0);
	echoptr->SetFrameTPS(0);


	m_loopCnt++;
}



