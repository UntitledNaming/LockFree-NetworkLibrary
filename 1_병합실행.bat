@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ============================================
echo  P1 병합 시작 - 잠시만 기다리세요
echo  (GitHub Desktop / Visual Studio 는 먼저 닫아주세요)
echo ============================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0finish_merge.ps1"
