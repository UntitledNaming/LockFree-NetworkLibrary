#!/usr/bin/env bash
# ============================================================
# LockFree-NetworkLibrary(P1) 병합 마무리 스크립트
# Windows Git Bash 에서 실행하세요.
#   1) 이 저장소 폴더에서 우클릭 → "Git Bash Here"
#   2)  bash finish_merge.sh   입력 후 엔터
# ------------------------------------------------------------
# 하는 일:
#   - 워킹트리를 마지막 로컬 커밋 상태로 정리(줄바꿈/머지 찌꺼기 제거)
#   - 원격(origin/main) 3개 커밋을 병합
#   - 빌드 산출물(.obj/.tlog/... ) 충돌은 자동으로 삭제 처리
#   - 진짜 소스 충돌(ChatServer.cpp)만 남겨서 직접 해결하도록 안내
# ============================================================
set -e

BUILD_EXT='\.(obj|o|tlog|pdb|idb|ilk|exe|dll|lib|exp|pch|ipch|iobj|ipdb|res|aps|suo|user|lastbuildstate|opendb|VC.db)$'

echo "▶ 0. 저장소 확인"
git rev-parse --is-inside-work-tree >/dev/null

echo "▶ 1. 워킹트리를 마지막 커밋 상태로 정리 (줄바꿈/머지 찌꺼기 폐기)"
git reset --hard HEAD

echo "▶ 2. 최신 원격 가져오기"
git fetch origin

echo "▶ 3. .gitignore 반영 + 이미 추적중인 빌드 산출물 추적 해제"
git add .gitignore
git ls-files -i -c -X .gitignore | tr '\n' '\0' | xargs -0 -r git rm -q --cached --ignore-unmatch
git commit -m "chore: add .gitignore, stop tracking build artifacts" || echo "  (커밋할 변경 없음 - 건너뜀)"

echo "▶ 4. 원격 병합 (충돌 예상됨 - 아래에서 자동/수동 처리)"
git merge origin/main || true

echo "▶ 5. 빌드 산출물 충돌은 전부 삭제로 자동 해결"
git diff --name-only --diff-filter=U | grep -Ei "$BUILD_EXT" | tr '\n' '\0' | xargs -0 -r git rm -f --ignore-unmatch || true
# 병합으로 다시 추적된 산출물도 정리
git ls-files -i -c -X .gitignore | tr '\n' '\0' | xargs -0 -r git rm -q --cached --ignore-unmatch || true

echo ""
echo "============================================================"
REMAIN=$(git diff --name-only --diff-filter=U)
if [ -z "$REMAIN" ]; then
  echo "✅ 남은 충돌 없음. 아래만 실행하면 끝:"
  echo "     git commit --no-edit"
  echo "     git push origin main"
else
  echo "⚠  아래 소스 파일 충돌만 직접 해결하세요 (에디터에서 <<<<<<< 표시 정리):"
  echo "$REMAIN" | sed 's/^/     /'
  echo ""
  echo "   해결 후:"
  echo "     git add <해결한파일>"
  echo "     git commit --no-edit"
  echo "     git push origin main"
fi
echo "============================================================"
