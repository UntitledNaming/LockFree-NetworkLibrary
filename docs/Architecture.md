# Architecture

## 1. 개요

이 프로젝트는 C++ IOCP를 기반으로 네트워크 라이브러리와 게임 라이브러리를 직접 구현하고, 그 위에 여러 서버를 올려 안정성과 확장성을 검증한 프로젝트입니다. 구조는 크게 네 부분으로 나뉩니다.

- `network_library` — IOCP 네트워크 코어(서버·세션·클라이언트).
- `game_library` — 네트워크 코어 위에 그룹·프레임 로직을 얹은 게임 라이브러리.
- `common_files` — 링버퍼, 직렬화 버퍼(Message), TLS 메모리 풀, Lock-Free 자료구조, 로그, 프로세스 모니터링 등 공용 모듈.
- `servers` — 위 라이브러리를 검증하는 여러 서버(채팅·로그인·모니터링·게임 등).

네트워크 라이브러리는 IOCP·세션·송수신만 담당하고, 각 서버는 콜백만 구현해 그 위에 올라갑니다. 즉 "네트워크 처리"와 "컨텐츠 처리"가 분리되어 있으며, 이 분리 덕분에 하나의 코어 위에 성격이 다른 서버들을 검증용으로 올릴 수 있습니다.

## 2. 전체 구조도

```
LockFree-NetworkLibrary
├─ network_library          # 네트워크 코어
│   ├─ CLanServer           #   LAN 환경 서버 (헤더=길이만)
│   ├─ CNetServer           #   Net 환경 서버 (코드·체크섬·인코딩)
│   ├─ CLanClient           #   클라이언트 코어
│   └─ CSession             #   세션 상태
├─ game_library             # 게임 라이브러리
│   ├─ CGameLibrary         #   네트워크 코어 + 그룹/프레임
│   ├─ CGroup               #   컨텐츠 그룹 (추상)
│   ├─ GameSession          #   그룹 소속 세션
│   └─ IUser                #   컨텐츠 유저 인터페이스
├─ common_files             # 공용 모듈
│   ├─ buffer               #   Ring_Buffer, CMessage
│   ├─ memorypool           #   MemoryPoolTLS, LockFreeMemoryPoolLive
│   ├─ lockfree             #   LFStack, LFQMultiLive, LFQSingleLive
│   ├─ dbtls                #   DBTLS (MySQL 스레드 로컬 커넥션)
│   └─ util                 #   LogClass, TextParser, ProcessMonitor(PDH), CPUUsage
├─ servers                  # 검증용 서버
│   ├─ chatserver           #   채팅 서버 (싱글 스레드: Update + Job 큐)
│   ├─ chatserver_multi_ver #   채팅 서버 (멀티 스레드: 락 기반)
│   ├─ chatserver_auth_ver  #   채팅 서버 (로그인 연동/인증, Redis)
│   ├─ loginserver          #   로그인 서버 (MySQL + Redis)
│   ├─ monitoringserver     #   모니터링 서버 (CLanServer)
│   └─ gameserver           #   게임 라이브러리 적용 게임(에코) 서버
├─ clients                  # 모니터링 클라이언트 (CMonitorClient)
├─ dummy                    # 부하 테스트용 더미 프로그램
├─ protocol                 # 서버 공통 프로토콜
├─ DB                       # DB 스키마
└─ redis                    # Redis 실행 파일
```

## 3. 네트워크 라이브러리 구조

네트워크 코어는 같은 IOCP 구조가 헤더 처리만 다르게 두 벌 존재합니다.

- `CLanServer` — LAN용. 헤더는 길이 정보(`LANHEADER`)만 있고 암호화·체크섬이 없습니다. 내부 서버 간 통신, 모니터링 서버가 사용합니다.
- `CNetServer` — 외부(Net)용. 헤더(`NETHEADER`)에 패킷 코드·길이·랜덤 키·체크섬이 있고, 페이로드를 인코딩(난독화)하고 수신 시 디코딩 하여 체크섬을 통해 인코딩 정합성을 검증합니다. 로그인·채팅 서버가 상속합니다.

주요 구성 요소:

- **IOCP / GQCS**: `CreateIoCompletionPort`로 완료 포트를 만들고, 러닝 스레드 수를 concurrent 값으로 지정합니다. Worker 스레드가 `GetQueuedCompletionStatus`로 완료를 받아, 완료에 실린 Overlapped 종류에 따라 Recv / Send / 세션 해제로 분기합니다.
- **Accept Thread**: 연결을 수락하고 세션을 배정한 뒤 소켓을 IOCP에 등록합니다. 이때 완료 키로 세션 포인터를 넣어, 이후 완료 처리에서 세션 조회 비용을 없앴습니다.
- **Worker Thread**: IOCP 완료를 처리하는 스레드 풀입니다. 수신 완료 시 패킷을 조립해 `OnRecv` 콜백으로 넘기고, 송신 완료 시 사용한 메시지를 반납합니다.
- **Send / Recv**: 수신은 세션의 링버퍼(`CRingBuffer`)로 조립하고, 송신은 세션별 Lock-Free 큐(`LFQueueMul`)에 모아 세션당 하나의 `WSASend`만 유지합니다. 선택적으로 Send 전용 스레드를 Config로 켤 수 있습니다.
- **SessionTable**: 세션은 고정 배열(`CSession[]`)로 관리하고, 빈 인덱스는 Lock-Free 스택(`LFStack`)으로 관리합니다. 세션 ID에 배열 인덱스와 할당 ID를 함께 인코딩해, 조회는 배열 인덱싱으로 빠르게 하고 재사용된 슬롯의 과거 세션을 구분합니다.
- **CSession**: 소켓, 세션 ID, 수신·송신용 Overlapped, 링버퍼, 송신 큐, 참조 카운트·릴리즈 플래그·연결 종료 플래그·송신 플래그를 가집니다.

## 4. 게임 라이브러리 구조

`CGameLibrary`는 `CNetServer`와 같은 네트워크 코어에 **그룹 시스템과 프레임 스레드**를 더한 버전입니다.

- **CGameLibrary**: 네트워크 코어 + 그룹 관리. `Attach`로 컨텐츠 그룹을 등록하고, 프레임 스레드가 각 그룹의 프레임 로직을 주기적으로 구동합니다. 그룹 이동은 완료 포트에 작업을 던지는 방식(PQCS)으로 처리합니다.
- **CGroup**: 컨텐츠가 상속하는 추상 클래스입니다. `OnClientJoin/OnClientLeave/OnRecv/OnIUserMove/OnUpdate` 콜백을 구현하고, 그룹 락으로 컨텐츠 처리를 직렬 또는 병렬로 제어합니다.
- **GameSession**: 네트워크 세션에 그룹 소속(그룹 ID)을 더한 세션입니다.
- **IUser**: 컨텐츠 유저의 기반 인터페이스입니다.

이 게임 라이브러리(`CGameLibrary` / `CGroup` / `GameSession` / `IUser`)는 P4 MMORPG 월드 서버의 기반으로 사용됩니다. P4는 `FieldGroup`·`AuthGroup`이 `CGroup`을 상속하고 `CUser`가 `IUser`를 상속하는 방식으로 이 구조를 그대로 이어받습니다. 이 레포의 게임(에코) 서버는 게임 라이브러리를 검증하기 위한 서버입니다.

## 5. 공통 모듈

- **Buffer / Message**: `CRingBuffer`는 수신 스트림을 조립하는 링버퍼, `CMessage`는 컨텐츠가 패킷을 만드는 직렬화 버퍼입니다. 두 버퍼 모두 재사용 구조로 관리됩니다.
- **TLS Memory Pool**: `CMPoolTLS`는 스레드별 서브풀(버킷 단위)로 객체를 할당·반납해 힙 경합을 줄입니다. 노드마다 풀 소유 ID를 심어 잘못된 반납을 차단합니다. `CMessage`가 이 풀로 관리됩니다.
- **Lock-Free Queue / Stack**: `LFQueueMul`(멀티 프로듀서 큐)은 세션 송신 큐로, `LFStack`은 세션 인덱스·메모리 풀 버킷 관리에 사용됩니다.
- **Parser / Log**: `TextParser`는 서버 Config 파싱, `LogClass`는 파일 로깅을 담당합니다.
- **ProcessMonitor / PDH**: Windows 성능 카운터(PDH)로 프로세스 CPU·메모리·TCP 재전송 등 시스템 지표를 수집합니다.

## 6. 테스트 서버 구조

라이브러리를 검증하기 위해 성격이 다른 서버를 여러 개 올렸습니다. 에코 서버는 그중 하나입니다.

- **Echo(Game) Server** — 게임 라이브러리(`CGameLibrary`)를 적용한 서버로, `CAuth`(입장/인증)·`CEcho`(에코 응답)·`CMonitor`(지표) 그룹으로 게임 라이브러리의 그룹·프레임·그룹 이동을 검증합니다.
- **Chat Server (싱글 스레드)** — `CNetServer` 위에 Update 스레드 + Job 큐로 컨텐츠를 직렬 처리하는 채팅 서버입니다.
- **Multi-thread Chat Server** — 같은 채팅 컨텐츠를 락(SRWLOCK) 기반으로 워커 스레드들이 병렬 처리하는 버전입니다. 싱글 버전과 비교해 컨텐츠 처리 방식의 성능 차이를 봅니다.
- **Login Server** — 계정 인증(MySQL)과 세션 토큰 발급(Redis)을 담당하고, 접속할 서버 라우팅 정보를 응답합니다.
- **Chat + Login Auth Server** — 로그인 서버가 발급한 토큰을 Redis로 검증하는 인증 버전 채팅 서버입니다.
- **Monitoring Server** — `CLanServer` 기반 중앙 수집 서버로, 각 서버의 지표를 받아 집계·저장합니다.

## 7. 설계상 특징

- **네트워크와 컨텐츠 분리**: 라이브러리는 IOCP·세션·송수신까지, 컨텐츠는 콜백에서 처리합니다. 덕분에 하나의 코어로 여러 서버를 검증할 수 있습니다.
- **OnRecv 콜백**: 라이브러리가 조립한 완성 패킷만 컨텐츠로 넘겨, 컨텐츠가 스트림 조립을 신경 쓰지 않게 합니다.
- **객체 재사용**: 세션·메시지·링버퍼·노드를 풀/재사용 구조로 관리해 반복 할당·해제 비용과 경합을 줄입니다.
- **모니터링 분리**: 지표 수집을 별도 모니터링 서버와 클라이언트로 분리해, 각 서버는 지표를 보내기만 하고 수집·집계는 한곳에서 처리합니다.

> 코드 흐름의 상세는 `docs/Code_Flow.md`, 각 구조를 그렇게 잡은 이유는 `docs/Design.md`를 참고하세요.
