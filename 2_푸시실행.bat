@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ============================================
echo  GitHub 로 푸시
echo ============================================

REM 병합이 끝났는지(충돌 없는지) 확인
git diff --name-only --diff-filter=U > "%temp%\_conf.txt"
for %%A in ("%temp%\_conf.txt") do set CONF_SIZE=%%~zA
if not "%CONF_SIZE%"=="0" (
  echo.
  echo [중단] 아직 병합이 안 끝났습니다. 먼저 1_병합실행.bat 을 실행하세요.
  type "%temp%\_conf.txt"
  pause
  exit /b 1
)

echo 푸시 중...
git push origin main
echo.
echo ============================================
echo  위에 에러(빨간 글씨)가 없으면 성공입니다.
echo  GitHub 페이지 새로고침해서 확인해 보세요.
echo ============================================
pause
