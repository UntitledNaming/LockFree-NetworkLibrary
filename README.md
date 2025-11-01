# LockFree-NetworkLibrary
Stable IOCP-based network library designed without session lock

< 폴더 설명 >
1. clients
   - LanClient 상속한 모니터링 클라이언트 클래스
     
2. common_files
   - 링버퍼, DBTLS, List, 락프리, 메모리 풀, 프로세스 모니터링 클래스(PDH 사용), 파서, 로그 클래스
   
3. docs
   - ppt
   - 각 종 서버 무중단 테스트 결과 및 멀티 스레드 테스트 결과

4. dummy
   - 채팅 서버, 채팅 서버 - 로그인 서버 연동, 게임 서버 더미 프로그램, 모니터링 프로그램(에이전트, 모니터링 수치 출력 프로그램)
     
5. game_library
   - 게임 라이브러리 및 그룹, 세션, 유저 인터페이스 클래스 헤더 및 소스 코드
   
6. network_library
   - 네트워크 라이브러리 헤더, 세션 및 Lan환경에서 서버, 클라이언트와 Net 환경에서 사용하는 서버 라이브러리 헤더 및 소스 코드
   
7. protocol
   - 서버에서 사용하는 전체 프로토콜
     
8. redis
   - redis 프로그램 
     
9. servers
   - 채팅 서버 (싱글 스레드 버전)
   - 채팅 서버 (인증 처리 구현 버전)
   - 채팅 서버 (멀티 스레드 버전)
   - 게임 서버 (게임 라이브러리 사용)
   - 로그인 서버
   - 모니터링 서버
     
10. tests
   - zero copy on / off 테스트

< 특징 요약 >
1. 세션 Lock 제거
   - 송신 락프리 큐 사용
   - 참조 카운트 기반 세션 유효성 관리
     
2. 라이브러리 구조
   - IOCP 기반 비동기 처리
   - Send 스레드 Config로 On / Off 가능
   - 세션 배열 및 index 관리(락프리 스택)
   - 세션 key 기반 통신
     
3. 게임 라이브러리 구조
   - 그룹 로직 처리(그룹 로직 싱글 처리, 그룹 이동시 PQCS 사용, 프레임 스레드로 그룹 프레임 로직 처리)
   - 네트워크 로직 처리
  

< 서버 구동 및 테스트 방법 : Release >

0. Mysql 설치
   - 폴더 경로 : C:\Program Files에 설치되어야 함.
   - 비밀번호 : 1q2w3e4r로 설정
   - DB 폴더 참조해 accountDB, logDB 스키마 설정하기
   - User And Privileges의 root 유저 클릭, Limit to Hosts Matching 항목을 %로 변경 후 저장.

1. 더미 프로그램 Config 설정
   - 서버 IP, Port 설정하기

2. 서버 구동 순서
   - 모니터링 서버 구동
   - 채팅 서버 더미 클라이언트 테스트 시 chatserver 구동
   - ChatDummy_Loginserver 테스트시 chatserver_auth 및 loginserver 구동
   - GameDummy 테스트 시 gameserver 구동

3. 모니터링 서버 구동 전 체크
   - DB IP, Port 설정
   - 외부 binding 할 IP, Port, 내부(lan 환경) binding할 IP, Port 설정

4. 채팅 서버 테스트 전 체크
   - CServerConfig 파일에 모니터링 서버 IP ,Port 설정
   - 모니터링 서버 먼저 구동

5. 채팅 서버(인증) 및 로그인 서버 연동 테스트 전 체크
   - chatserver_auth Config 파일에 Redis IP, Port와 모니터링 서버 IP, Port 설정
   - 로그인 서버 LServerConfig 파일에서 DB IP, PORT와 REDIS IP, PORT 설정
   - SERVERRoutingConfig 파일에 더미 프로그램 동작 시킬 시스템의 IP를 설정하고 해당 IP를 로그인 서버가 확인시 로그인 응답으로 전달할 IP(채팅 서버, 게임 서버 IP) 저장 및 채팅 서버, 게임 서버 포트 번호 저장
   - 모니터링 서버 및 Redis 서버 먼저 구동
     
6. 게임 서버 테스트 전 체크
   - CMonitorConfig에 모니터링 서버 IP, Port 설정
   - 모니터링 서버 먼저 구동
    







