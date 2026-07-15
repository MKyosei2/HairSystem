@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
python -m unittest discover -s "%ROOT%\tests\final_stage" -p "test_*.py" -v
exit /b %ERRORLEVEL%
