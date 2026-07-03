# ============================================================
# LockFree-NetworkLibrary(P1) 병합 마무리 - PowerShell 버전
# 실행: 이 폴더에서 PowerShell 열고 아래 입력
#     powershell -ExecutionPolicy Bypass -File finish_merge.ps1
# ============================================================
$ErrorActionPreference = "Continue"
$build = '\.(obj|o|tlog|pdb|idb|ilk|exe|dll|lib|exp|pch|ipch|iobj|ipdb|res|aps|suo|user|lastbuildstate|opendb)$'

Set-Location -Path $PSScriptRoot
git rev-parse --is-inside-work-tree | Out-Null

Write-Host "1. 워킹트리를 마지막 커밋 상태로 정리 (줄바꿈/머지 찌꺼기 폐기)" -ForegroundColor Cyan
git reset --hard HEAD

Write-Host "2. 최신 원격 가져오기" -ForegroundColor Cyan
git fetch origin

Write-Host "3. .gitignore 반영 + 빌드 산출물 추적 해제" -ForegroundColor Cyan
git add .gitignore
git -c core.quotepath=false ls-files -i -c -X .gitignore | ForEach-Object { if ($_){ git rm -q --cached --ignore-unmatch -- "$_" } }
git commit -m "chore: add .gitignore, stop tracking build artifacts"

Write-Host "4. 원격 병합 (충돌 예상 - 아래에서 처리)" -ForegroundColor Cyan
git merge origin/main

Write-Host "5. 빌드 산출물 충돌은 전부 삭제로 자동 해결" -ForegroundColor Cyan
git -c core.quotepath=false diff --name-only --diff-filter=U | Where-Object { $_ -match $build } | ForEach-Object { git rm -f --ignore-unmatch -- "$_" }
git -c core.quotepath=false ls-files -i -c -X .gitignore | ForEach-Object { if ($_){ git rm -q --cached --ignore-unmatch -- "$_" } }

Write-Host "============================================================" -ForegroundColor Yellow
$remain = git -c core.quotepath=false diff --name-only --diff-filter=U
if (-not $remain) {
    Write-Host "OK 남은 충돌 없음. 아래만 실행하면 끝:" -ForegroundColor Green
    Write-Host "     git commit --no-edit"
    Write-Host "     git push origin main"
} else {
    Write-Host "아래 소스 파일 충돌만 직접 해결하세요 (에디터에서 <<<<<<< 표시 정리):" -ForegroundColor Red
    $remain | ForEach-Object { Write-Host "     $_" }
    Write-Host ""
    Write-Host "해결 후:  git add <파일> ; git commit --no-edit ; git push origin main"
}
Write-Host "============================================================" -ForegroundColor Yellow
