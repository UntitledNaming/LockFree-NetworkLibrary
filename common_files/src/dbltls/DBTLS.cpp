
#include <windows.h>
#include <iostream>
#include <string>
#include <mysql.h>
#include <strsafe.h>
#include <unordered_map>
#include "LogClass.h"
#include "DBTLS.h"


DBTLS::DBTLS(const CHAR* DBip, INT DBPort) 
{
	m_TlsIdx = TlsAlloc();
	if (m_TlsIdx == TLS_OUT_OF_INDEXES)
	{
		__debugbreak();
	}

	m_DBIP = DBip;
	m_DBPort = DBPort;
	m_DBQArrayIdx = DBTLS_IDX;

	mysql_library_init(0, NULL, NULL);
}

DBTLS::~DBTLS()
{

}

bool DBTLS::DB_Post_Query(const CHAR* QueryString, ...)
{
	DB_Query* ret = nullptr;
	INT16     retIDX;

	ret = (DB_Query*)TlsGetValue(m_TlsIdx);
	if (ret == nullptr)
	{
		// 쿼리 처음 호출
		ret = new DB_Query(this,m_DBIP.c_str(),m_DBPort);

		// 관리 배열범위 체크
		retIDX = InterlockedIncrement16(& m_DBQArrayIdx);
		if (retIDX >= DBTLS_MAX_COUNT)
		{
			delete ret;
			return false;
		}

		TlsSetValue(m_TlsIdx, ret);

		// 관리 배열에 저장
		m_DBQueryAry[retIDX] = ret;


	}

	va_list args;
	va_start(args, QueryString);
	ret->DB_Post_Query(QueryString, args);
	va_end(args);

	return true;
}

MYSQL_RES* DBTLS::DB_GET_Result(int type)
{
	DB_Query* ret = nullptr;
	ret = (DB_Query*)TlsGetValue(m_TlsIdx);

	return ret->DB_GET_Result(type);
}

MYSQL_ROW* DBTLS::DB_Fetch_Row(MYSQL_RES* res)
{
	DB_Query* ret = nullptr;
	ret = (DB_Query*)TlsGetValue(m_TlsIdx);
	return ret->DB_Fetch_Row(res);
}

void DBTLS::DB_Free_Result()
{
	DB_Query* ret = nullptr;
	ret = (DB_Query*)TlsGetValue(m_TlsIdx);
	ret->DB_Free_Result();
}

DBTLS::DB_Query::DB_Query(DBTLS* parent, const CHAR* DBip, UINT DBPort) : m_Parent(parent), m_sql_result(nullptr), m_sql_row(nullptr)
{
	mysql_init(&m_Conn);

	m_Connection = mysql_real_connect(&m_Conn, DBip, "root", "1q2w3e4r", "logdb", DBPort, (char*)NULL, 0);
	if (m_Connection == NULL)
	{
		LOG(L"DB", en_LOG_LEVEL::dfLOG_LEVEL_ERROR, L"DB Connect Error... / UniqID : %s ", mysql_error(&m_Conn));
		__debugbreak();
	}


}

DBTLS::DB_Query::~DB_Query()
{
	mysql_close(m_Connection);
}

bool DBTLS::DB_Query::DB_Post_Query(const CHAR* QueryString, const va_list& args)
{
	INT     query_stat;
	HRESULT ret;
	CHAR*  pBuffer = (CHAR*)malloc(DBQUERY_DEFAULT_LEN);
	BOOL    reSize = false;



    ret = StringCchVPrintfA(pBuffer, DBQUERY_DEFAULT_LEN, QueryString, args);
    

    // 쿼리 스트링 길이가 할당 크기보다 크면 중단
	if (ret == STRSAFE_E_INSUFFICIENT_BUFFER)
	{
		free(pBuffer);
		return false;
	}

	query_stat = mysql_query(m_Connection, pBuffer);
	if (query_stat != 0)
	{
		int error_code = mysql_errno(m_Connection);
		// DB와 연결 끊겨서 에러 날 수도 있음. 이때 이 세션 그냥 끊고 
		// 유저가 다시 연결 해서 로그인 요청 보내게 하는 방법
		LOG(L"DB",en_LOG_LEVEL::dfLOG_LEVEL_ERROR,L"DB mysql_query Error : %s" ,mysql_error(&m_Conn));
		free(pBuffer);
		return false;
	}

	free(pBuffer);
	return true;
}

MYSQL_RES* DBTLS::DB_Query::DB_GET_Result(int type)
{
	if (type == 0)
	{
		m_sql_result = mysql_store_result(m_Connection);
		return m_sql_result;
	}

	else if (type == 1)
	{
		m_sql_result = mysql_use_result(m_Connection);
		return m_sql_result;
	}


	return nullptr;
}

MYSQL_ROW* DBTLS::DB_Query::DB_Fetch_Row(MYSQL_RES* res)
{
	if (m_sql_result != res)
		__debugbreak();

	m_sql_row = mysql_fetch_row(m_sql_result);
	if (m_sql_row == NULL)
	{
		return nullptr;
	}

	return &m_sql_row;
}

void DBTLS::DB_Query::DB_Free_Result()
{
	mysql_free_result(m_sql_result);
}
