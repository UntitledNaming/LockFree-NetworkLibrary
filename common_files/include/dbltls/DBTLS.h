#pragma once

#define DBTLS_MAX_COUNT     50
#define DBTLS_IDX           -1
#define DBQUERY_DEFAULT_LEN 200


class DBTLS
{

public:
	enum DB_RESULT_TYPE
	{
		en_Store = 0,
		en_Use,
	};

public:
	DBTLS();
	DBTLS(const CHAR* DBip, INT DBPort);
	~DBTLS();
	

	bool        DB_Post_Query(const CHAR* QueryString, ...);                       // DB�� ���� ������ �Լ�
    MYSQL_RES*  DB_GET_Result(int type);                                           // type 0 : mysql_store_result / type 1 : mysql_use_result
    MYSQL_ROW*  DB_Fetch_Row(MYSQL_RES* res);                                      // DB�� ���� ������ Row�� 1�� ��� ���� �� 
                                                                                   // �ش� Row ���ڿ� ����� ������ ��ȯ
    void        DB_Free_Result(); 

public:
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DB_TLS ���� Ŭ����
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	class DB_Query
	{
	public:
		MYSQL      m_Conn;
		MYSQL*     m_Connection;
		MYSQL_RES* m_sql_result;
		MYSQL_ROW  m_sql_row;
		DBTLS*     m_Parent;


	public:
		DB_Query(DBTLS* parent, const CHAR* DBip, UINT DBPort);
		~DB_Query();

		bool        DB_Post_Query(const CHAR* QueryString, const va_list& args);   // DB�� ���� ������ �Լ�

		MYSQL_RES*  DB_GET_Result(int type);                                       // type 0 : mysql_store_result / type 1 : mysql_use_result
															                       
		MYSQL_ROW*  DB_Fetch_Row(MYSQL_RES* res);                                  // DB�� ���� ������ Row�� 1�� ��� ���� �� 
		                                                                           // �ش� Row ���ڿ� ����� ������ ��ȯ
		void        DB_Free_Result();                   
	};


private:
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DB_TLS Ŭ���� ��� ����
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	DWORD             m_TlsIdx;
	std::string       m_DBIP;
	UINT              m_DBPort;
	DB_Query*         m_DBQueryAry[DBTLS_MAX_COUNT];
	INT16             m_DBQArrayIdx;


};


