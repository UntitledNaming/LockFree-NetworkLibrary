# ============================================================
# LockFree-NetworkLibrary(P1) 병합 엔진 (완전 자동)
# "1_병합실행.bat" 을 더블클릭하면 이게 돌아갑니다.
# ------------------------------------------------------------
#  - 워킹트리 정리 → 원격 병합
#  - 빌드 산출물 충돌: 자동 삭제
#  - ChatServer.cpp 충돌: 우리 버전(정상 한글 UTF-8) 자동 채택
#  - .gitignore 반영 → 병합 커밋까지 자동 완료
#  * 끝나면 "2_푸시실행.bat" 만 더블클릭하면 GitHub 반영
# ============================================================
$ErrorActionPreference = "Continue"
$build = '\.(obj|o|tlog|pdb|idb|ilk|exe|dll|lib|exp|pch|ipch|iobj|ipdb|res|aps|suo|user|lastbuildstate|opendb)$'

Set-Location -Path $PSScriptRoot
if (-not (Test-Path ".git")) { Write-Host "여기는 git 저장소가 아닙니다." -ForegroundColor Red; Read-Host; exit 1 }

# 커밋 identity 없으면 기존 작성자로 설정
if (-not (git config user.email)) {
    git config user.name  "UntitledNaming"
    git config user.email "117499155+UntitledNaming@users.noreply.github.com"
}

Write-Host "1. 워킹트리를 마지막 커밋 상태로 정리" -ForegroundColor Cyan
git reset --hard HEAD

Write-Host "2. 최신 원격 가져오기" -ForegroundColor Cyan
git fetch origin

Write-Host "3. 원격 병합 (충돌은 아래에서 자동 처리)" -ForegroundColor Cyan
git merge --no-edit origin/main

Write-Host "4. ChatServer.cpp 충돌 -> 우리 버전(정상 UTF-8) 자동 채택" -ForegroundColor Cyan
git -c core.quotepath=false diff --name-only --diff-filter=U | Where-Object { $_ -match 'ChatServer\.cpp$' } | ForEach-Object {
    git checkout --ours -- "$_" ; git add -- "$_" ; Write-Host "   채택(ours): $_"
}

Write-Host "5. 빌드 산출물 충돌 -> 자동 삭제 + .gitignore 반영" -ForegroundColor Cyan
git -c core.quotepath=false diff --name-only --diff-filter=U | Where-Object { $_ -match $build } | ForEach-Object { git rm -f --ignore-unmatch -- "$_" | Out-Null }
git add .gitignore
git -c core.quotepath=false ls-files -i -c -X .gitignore | ForEach-Object { if ($_){ git rm -q --cached --ignore-unmatch -- "$_" | Out-Null } }

$remain = git -c core.quotepath=false diff --name-only --diff-filter=U
Write-Host ""
Write-Host "============================================================" -ForegroundColor Yellow
if (-not $remain) {
    git commit --no-edit | Out-Null
    Write-Host "[완료] 병합 자동 해결 + 커밋까지 끝났습니다." -ForegroundColor Green
    Write-Host "       이제 '2_푸시실행.bat' 을 더블클릭하면 GitHub에 올라갑니다." -ForegroundColor Green
} else {
    Write-Host "[확인 필요] 예상 못한 충돌이 남았습니다:" -ForegroundColor Red
    $remain | ForEach-Object { Write-Host "     $_" }
    Write-Host " 이 화면을 캡처해서 물어봐 주세요." -ForegroundColor White
}
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host ""
Read-Host "엔터를 누르면 창이 닫힙니다"
