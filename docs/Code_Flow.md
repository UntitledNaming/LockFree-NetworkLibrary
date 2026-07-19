# Code Flow

이 문서는 P1의 코드 흐름을 파일별 나열이 아니라 기능 흐름 기준으로 정리합니다. README보다 자세하되, 함수 전체 목록이 아니라 각 흐름의 핵심 함수만 설명합니다. getter/setter, 단순 helper는 생략합니다.

네트워크 코어는 `CLanServer`(LAN)와 `CNetServer`(Net)가 거의 같은 구조이므로, 기본 서술은 `CNetServer`를 기준으로 하고 차이가 있는 지점만 표시합니다.

---

## 1. 서버 시작 / 초기화 흐름

- 목적: IOCP·세션 테이블·스레드를 준비해 수신 대기 상태로 만든다.
- 관련 파일: `network_library/src/CNetServer.cpp`, `game_library/src/CGameLibrary.cpp`, 각 서버의 `main`
- 관련 클래스: `CNetServer` / `CLanServer` / `CGameLibrary`
- 관련 함수: `Start()`(Lan/Net) 또는 `Run()`(GameLib), `Mem_Init()`, `Net_Init()`, `Thread_Create()`
- 처리 순서:
  1. 컨텐츠 서버가 Config를 파싱한 뒤 부모의 `Start()`를 호출한다.
  2. `Mem_Init()`: 세션 테이블(`new CSession[]`)과 인덱스용 `LFStack`을 만들고, `CreateIoCompletionPort`로 IOCP를 생성한다(러닝 스레드 수를 concurrent 값으로 지정).
  3. `Net_Init()`: `WSAStartup` → 리슨 소켓 생성 → Nagle 설정 → `bind` → `listen`.
  4. `Thread_Create()`: Worker 스레드 N개 + Accept 스레드 1개를 만들고, Config에서 Send 스레드 플래그가 켜져 있으면 Send 스레드 1개를 추가한다.
- 설계 의도: IOCP concurrent 값을 러닝 스레드 수로 지정해 동시에 깨어나는 워커 수를 조절하고, 세션 테이블을 고정 배열 + Lock-Free 인덱스 스택으로 관리해 세션 할당/반환을 빠르게 한다.
- 관련 테스트: 장시간 무중단 테스트에서 초기화·종료 안정성 확인.
- 확인 필요: `CGameLibrary.cpp`의 초기화 세부(프레임 스레드 생성 지점)는 헤더 기준으로 정리했으며 본문 재확인 권장.

---

## 2. Accept 흐름

- 목적: 새 연결을 받아 세션을 배정하고 IOCP에 등록해 수신을 시작한다.
- 관련 파일: `network_library/src/CNetServer.cpp` (`AcceptThread`)
- 관련 클래스: `CNetServer`, `CSession`
- 관련 함수: `AcceptThread()`, `MakeSessionID()`, `CSession::Init()`, `RecvPost()`, `Release()`
- 처리 순서:
  1. `accept`로 클라이언트 소켓을 받고 현재 세션 수를 확인한다(최대치 초과 시 끊음).
  2. `OnConnectionRequest` 콜백으로 IP 필터링 기회를 준다.
  3. 소켓 옵션을 설정한다: 송신 버퍼 0(중첩 IO/제로카피 목적), Nagle, `SO_LINGER`(종료 시 즉시 RST).
  4. 인덱스 스택에서 빈 세션 인덱스를 꺼내 `MakeSessionID(index, allocID)`로 세션 ID를 만든다(= 인덱스를 상위 비트, 할당 ID를 하위 비트에 인코딩).
  5. `CSession::Init`으로 세션을 초기화하고 `CreateIoCompletionPort`로 소켓을 IOCP에 등록한다. **완료 키로 세션 포인터 자체를 넣는다.**
  6. `OnClientJoin` 콜백 → `RecvPost`로 첫 수신 등록 → Accept가 잡고 있던 참조를 `Release`.
- 설계 의도: 완료 키에 세션 포인터를 넣어 이후 완료 처리에서 세션 조회 비용을 없앤다. 세션 ID에 인덱스를 실어 조회를 배열 인덱싱 한 번으로 끝낸다. `SO_LINGER 0`으로 재사용 소켓의 종료 상태 누적을 피한다.
- 관련 테스트: 장시간 접속/해제 반복에서 세션 누수·중복 접근 확인.
- 확인 필요: 없음(코드 확인).

---

## 3. Recv 완료 처리 흐름

- 목적: 수신 완료를 받아 링버퍼에서 패킷을 조립하고 컨텐츠로 넘긴다.
- 관련 파일: `network_library/src/CNetServer.cpp` (`WorkerThread`, `RecvIOProc`), `common_files/.../Ring_Buffer.h`, `.../CMessage.h`
- 관련 클래스: `CNetServer`, `CSession`, `CRingBuffer`, `CMessage`
- 관련 함수: `WorkerThread()`, `RecvIOProc()`, `RecvPost()`, `CMessage::Alloc/Free`, `OnRecv()`
- 처리 순서:
  1. Worker의 `GetQueuedCompletionStatus`가 완료를 받는다. 완료에 실린 Overlapped가 수신용이면 `RecvIOProc`로 진입한다.
  2. 수신 크기가 0이면 정상 종료로 보고 연결 종료 플래그를 세운다.
  3. 링버퍼의 쓰기 위치를 수신 크기만큼 옮긴 뒤, 버퍼가 빌 때까지 반복해 패킷을 조립한다: 헤더를 확인하고(Net은 패킷 코드·길이 검증), 완성된 페이로드를 `CMessage`로 복사한다. Net은 여기서 디코딩 후 체크섬을 검증하고, 어긋나면 `Disconnect`.
  4. 조립된 패킷마다 `OnRecv(sessionID, message)` 콜백을 호출한다.
  5. 연결 종료가 아니면 `RecvPost`로 다음 수신을 등록하고, 참조를 `Release`.
- 설계 의도: TCP 스트림 경계 문제를 링버퍼 + 헤더 길이 기반 조립으로 처리한다. Net은 코드·길이·체크섬 3중 검증으로 조작 패킷을 차단한다.
- 관련 테스트: 장시간 테스트, 부하 테스트에서 조립 정확성·안정성 확인.
- 확인 필요: `CMessage`/`CRingBuffer`의 오프셋 계산 세부는 구현부 재확인 권장.

---

## 4. Send 요청 흐름

- 목적: 컨텐츠가 만든 메시지를 세션 송신 큐에 넣고 송신을 건다.
- 관련 파일: `network_library/src/CNetServer.cpp` (`SendPacket`, `SendPost`)
- 관련 클래스: `CNetServer`, `CSession`, `CMessage`, `LFQueueMul`
- 관련 함수: `SendPacket()`, `SendPacketAll()`, `SendPost()`, `Encoding()`(Net)
- 처리 순서:
  1. 컨텐츠가 `SendPacket(sessionID, message)`를 호출한다.
  2. 세션 유효성을 확인하고, 송신 큐가 한도를 넘으면 밀린 것으로 보고 `Disconnect`.
  3. 헤더를 만든다. Lan은 길이만, Net은 코드·길이·랜덤 키·체크섬을 채우고 `Encoding`으로 페이로드를 인코딩한다(중복 인코딩 방지 플래그 사용).
  4. 메시지 참조 카운트를 올리고 세션의 송신 큐(`LFQueueMul`)에 넣는다.
  5. Send 스레드를 쓰지 않는 설정이면 즉시 `SendPost`를 호출한다.
- SendPost 핵심:
  - 세션당 송신 플래그를 원자적으로 잡아 **세션당 하나의 `WSASend`만** 유지한다.
  - 송신 큐에서 여러 메시지를 한 번에 꺼내 `WSABUF` 배열로 묶어 `WSASend`를 건다.
- 설계 의도: 여러 스레드의 송신을 세션 큐로 모으고, 송신 플래그로 세션당 송신을 하나로 유지해 중복 송신·순서 꼬임을 세션 락 없이 막는다.
- 관련 테스트: 무중단·부하 테스트에서 송신 안정성 확인.
- 확인 필요: 없음(코드 확인).

---

## 5. Send 완료 처리 흐름

- 목적: 송신 완료를 받아 메시지를 반납하고 남은 송신을 이어간다.
- 관련 파일: `network_library/src/CNetServer.cpp` (`SendIOProc`)
- 관련 클래스: `CNetServer`, `CSession`, `CMessage`
- 관련 함수: `SendIOProc()`, `CMessage::Free()`, `SendPost()`, `Release()`
- 처리 순서:
  1. Worker에서 송신용 완료로 확인되면 `SendIOProc`로 진입한다.
  2. 방금 보낸 메시지들을 `CMessage::Free`로 메모리 풀에 반납한다(Send 요청 때 올린 참조와 짝).
  3. 송신 플래그를 해제하고, Send 스레드를 안 쓰는 설정이면 그 자리에서 다시 `SendPost`로 큐에 쌓인 다음 묶음을 보낸다.
  4. 참조를 `Release`.
- 설계 의도: 송신 완료 스레드가 곧바로 다음 송신을 이어 별도 Send 스레드 없이도 처리 절차이 흐르게 한다.
- 관련 테스트: 무중단 테스트.
- 확인 필요: 없음(코드 확인).

---

## 6. 세션 생성 / 해제 흐름

- 목적: 세션 수명을 락 없이 안전하게 관리하고 중복 해제를 막는다.
- 관련 파일: `network_library/src/CNetServer.cpp` (`Disconnect`, `SessionInvalid`, `Release`, `ReleaseProc`), `CSession.cpp`, `common_files/.../LFStack.h`
- 관련 클래스: `CSession`, `LFStack`
- 관련 함수: `MakeSessionID()`, `FindSession()`, `Disconnect()`, `SessionInvalid()`, `Release()`, `ReleaseProc()`
- 처리 순서:
  1. 세션 ID는 `(index << 상위비트) | allocID`로 생성한다. `FindSession`은 상위 비트를 시프트해 배열 인덱스를 얻어 세션을 즉시 가리킨다.
  2. 사용 진입 시 `SessionInvalid`가 참조 카운트를 올리고, 이미 해제 중이거나 세션 ID가 불일치하면 되돌린다(재사용된 슬롯의 과거 요청 차단).
  3. `Disconnect`: 연결 종료 플래그를 최초 1회만 세우고, 걸린 수신·송신 IO를 `CancelIoEx`로 취소한 뒤 `Release`.
  4. `Release`: 참조 카운트와 릴리즈 플래그를 **128비트 CAS 한 번**으로 검사해, 참조가 0이고 아직 해제 중이 아닐 때만 성공한다. 성공하면 완료 포트에 해제 작업을 던진다(PQCS).
  5. `ReleaseProc`(Worker에서 실행): 남은 메시지 반납 → `OnClientLeave` → 소켓 닫기 → 인덱스 스택에 반환 → 세션 수 감소.
- 설계 의도: 세션 단위 뮤텍스 없이 참조 카운트 + 릴리즈 플래그를 하나의 128비트 CAS로 묶어 "마지막 사용자가 안전하게 반납"을 보장한다.
- 관련 테스트: 장시간 접속/해제 반복.
- 확인 필요: `Release`의 인자 중 사용되지 않는 값이 있으나 실제 판정은 128비트 CAS로 한다(설명 시 오해 방지).

---

## 7. TLS 메모리 풀 사용 흐름

- 목적: 스레드 경합 없이 메시지 등 객체를 할당/반납한다.
- 관련 파일: `common_files/.../MemoryPoolTLS.h`, `.../CMessage.h`
- 관련 클래스: `CMPoolTLS`, `CMessage`
- 관련 함수: `CMPoolTLS::Alloc/Free`, `CMessage::Alloc/Free`
- 처리 순서:
  1. `CMessage::Alloc`은 정적 TLS 풀의 `Alloc`을 호출한다.
  2. `Alloc`은 현재 스레드의 서브풀을 얻어(없으면 새로 만들어) 버킷에서 객체를 꺼낸다. 버킷이 비면 공용 Lock-Free 스택에서 버킷을 가져온다.
  3. `Free`는 노드의 풀 소유 ID가 이 풀 것이 아니면 반납을 거부하고, 맞으면 반환 버킷에 쌓는다. 반환 버킷이 차면 공용 스택으로 넘긴다.
- 설계 의도: 스레드별 서브풀로 대부분의 할당/반납을 락 없이 처리하고, 버킷 단위로만 공용 자료구조를 오가 경합 빈도를 줄인다. 소유 ID로 잘못된 반납을 차단한다.
- 관련 테스트: 부하·무중단 테스트에서 풀 사용량 관측.
- 확인 필요: 없음(코드 확인).

---


## 8. 채팅 서버 흐름 (싱글/멀티 비교)

- 목적: 네트워크 라이브러리 위에 섹터 기반 채팅 컨텐츠를 올려 검증하고, 컨텐츠 처리의 싱글/멀티 방식을 비교한다.
- 관련 파일: `servers/chatserver/ChatServer.*`(싱글), `servers/chatserver_multi_ver/*`(멀티)
- 관련 클래스: `ChatServer`(`CNetServer` 상속), `CUser`, 섹터 배열
- 관련 함수: `OnRecv()`, `UpdateThread()`(싱글), `LoginProc/SectorMoveProc/ChatMessageProc`, 섹터 브로드캐스트 함수
- 처리 순서:
  1. 싱글판: 워커의 `OnRecv`가 메시지를 Job으로 만들어 Lock-Free 큐에 넣고, 단일 Update 스레드가 순차 처리한다(컨텐츠 자료구조에 락 불필요).
  2. 멀티판: Update 스레드/Job 큐 없이, 워커 스레드들이 유저 맵·섹터에 락(SRWLOCK)을 걸고 컨텐츠를 직접 병렬 처리한다.
  3. 채팅 요청 시 주변 섹터의 유저에게 메시지를 확산 전송한다.
- 설계 의도: 싱글은 "네트워크는 멀티 워커, 컨텐츠는 단일 스레드 직렬화(락 없음)", 멀티는 "컨텐츠도 락으로 병렬 처리". 두 방식의 성능 차이를 비교한다.
- 관련 테스트: 싱글/멀티 채팅 서버 비교, 채팅 7일 무중단.
- 확인 필요: 멀티판의 락 범위·브로드캐스트 세부는 구현부 재확인 권장.

---

## 9. 로그인 / 인증 흐름

- 목적: 계정 인증 후 세션 토큰을 발급하고, 접속할 서버 라우팅 정보를 응답한다.
- 관련 파일: `servers/loginserver/.../CLoginServer.cpp`, `servers/chatserver_auth_ver/.../ChatServer.cpp`, `common_files/.../DBTLS.h`, `redis/`
- 관련 클래스: `CLoginServer`(`CNetServer` 상속), `DBTLS`, `cpp_redis::client`
- 관련 함수: `OnRecv()`, `LoginRequest()`, `GetDBData()`, `SetRedisToken()`, (인증 채팅) `LoginProc()`의 Redis 조회/삭제
- 처리 순서:
  1. 로그인 서버 `OnRecv`가 로그인 요청을 받으면 `LoginRequest`로 계정 번호·세션 키를 추출한다.
  2. `GetDBData`로 계정 DB(MySQL)에서 ID·닉네임을 조회한다.
  3. `SetRedisToken`으로 Redis에 세션 토큰을 저장한다(계정 번호 → 토큰).
  4. 접속할 게임/채팅 서버 IP·Port를 찾아 클라이언트에 응답한다.
  5. 인증 버전 채팅 서버는 접속 시 Redis에서 토큰을 조회해 클라이언트가 보낸 토큰과 대조하고, 일치하면 인증 통과 후 토큰을 삭제한다(1회용).
- 설계 의도: 로그인 서버가 Redis에 토큰을 쓰고, 컨텐츠 서버가 Redis에서 읽어 검증하는 구조로 서버 간 인증을 연결한다.
- 관련 테스트: 채팅-로그인 5일 인증 테스트.
- 확인 필요: 게임 서버의 인증 그룹은 Redis 토큰 검증 없이 그룹 이동만 하는 것으로 보이며 인증 강도 재확인 권장.

---

## 10. 모니터링 서버 흐름

- 목적: 각 서버의 지표를 한곳에 모아 저장·관찰한다.
- 관련 파일: `servers/monitoringserver/*`, `clients/.../CMonitorClient.*`, `common_files/.../ProcessMonitor.*`
- 관련 클래스: `CMonitor`(`CLanServer` 상속), `CMonitorClient`(`CLanClient` 상속), `ProcessMonitor`
- 관련 함수: (서버) `RunServer()`, `OnRecv()`; (클라) `SendMonitorData()`; (지표) PDH 카운터 수집
- 처리 순서:
  1. 각 서버는 `CMonitorClient`로 모니터링 서버에 접속하고, `ProcessMonitor`(PDH)로 CPU·메모리·TCP 재전송 등을 수집한다.
  2. 주기적으로 `SendMonitorData`로 지표를 LAN으로 전송한다.
  3. 모니터링 서버 `OnRecv`가 지표를 받아 서버별로 집계하고 저장한다.
- 설계 의도: 지표 수집을 별도 서버로 분리해, 각 서버는 보내기만 하고 수집·집계는 한곳에서 처리한다.
- 관련 테스트: 장시간 테스트 중 지표 관측.
- 확인 필요: 모니터링 서버의 DB 적재·집계 세부는 구현부 재확인 권장.

---

## 11. 게임 라이브러리 / 게임 서버 흐름

- 목적: 네트워크 코어 위에 그룹·프레임 시스템을 얹은 게임 라이브러리를 검증한다.
- 관련 파일: `game_library/*`, `servers/gameserver/*`
- 관련 클래스: `CGameLibrary`, `CGroup`, `GameSession`, `IUser`, 컨텐츠 `CAuth/CEcho/CMonitor`(`CGroup` 상속)
- 관련 함수: `CGameLibrary::Run/Attach/GroupMove/FrameThread`, `CGroup::OnRecv/OnUpdate/OnIUserMove`
- 처리 순서:
  1. `main`이 `CGameLibrary`를 만들고 `Attach`로 컨텐츠 그룹(입장/에코/지표)을 등록한 뒤 `Run`.
  2. 프레임 스레드가 각 그룹의 주기가 되면 완료 포트로 프레임 작업을 던지고, 워커가 그룹의 `OnUpdate`를 실행한다.
  3. 수신 패킷은 세션이 속한 그룹의 `OnRecv`로 전달된다. 인증 통과 시 `GroupMove`로 세션을 다른 그룹으로 옮긴다.
- 설계 의도: 그룹·프레임·그룹 이동으로 컨텐츠를 모듈화한다. 이 구조가 P4 MMORPG 서버의 기반이 된다.
- 관련 테스트: 게임 서버 7일 연결 테스트.
- 확인 필요: `CGameLibrary.cpp`의 프레임/그룹 이동 처리 세부는 헤더 기준으로 정리했으며 본문 재확인 권장.

---

## 12. 더미 클라이언트 / 부하 테스트 흐름

- 목적: 다수 접속·송수신을 만들어 서버 안정성과 측정 공정성을 검증한다.
- 관련 파일: `dummy/`(실행 파일·로그·설정), 관찰 기록 `docs/servertest_results/ZeroCopy Test/테스트 고찰.txt`
- 관련 클래스: 확인 불가(더미 클라이언트 소스는 이 레포에 코드로 포함되어 있지 않고 실행 파일·로그만 있음)
- 처리 순서(관찰 기록 기준):
  1. 더미는 다수 소켓으로 접속해 반복 송수신하며 부하를 만든다.
  2. 한 클라이언트 스레드가 수백 개 소켓의 수신을 다 처리하지 못하면 TCP 수신 버퍼가 차고, window size가 0이 되어 서버 송신이 완료 통지를 받지 못해 서버 처리량이 떨어진다.
  3. 이 병목은 서버가 아니라 측정 클라이언트 쪽이며, 측정 환경을 보정해야 결과가 신뢰된다.
- 설계 의도: 측정 도구 자체의 병목까지 데이터로 확인해 측정의 공정성을 확보한다.
- 관련 자료: 측정 관찰 기록(`docs/servertest_results/ZeroCopy Test/테스트 고찰.txt`), `docs/Test_Report.md`.
- 확인 필요: 더미 클라이언트 소스가 이 레포에 없어, 관련 흐름은 측정 문서의 관찰 기록 기준이다.

> 각 흐름을 그렇게 설계한 이유는 `docs/Design_Rationale.md`, 측정 결과·수치는 `docs/Test_Report.md`, 문제 해결 과정은 `docs/Troubleshooting.md`를 참고하세요.
