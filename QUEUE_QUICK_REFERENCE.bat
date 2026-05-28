@echo off
REM Quick reference for Queue Infrastructure on Windows WSL
REM This is a helper script to show commands

echo ===================================
echo Silicon Patient Platform - Queue Infrastructure
echo Windows WSL Quick Reference
echo ===================================
echo.

echo Step 1: Start Redis in WSL
echo   ubuntu run redis-server
echo.

echo Step 2: Activate Python venv (Terminal 1)
echo   cd backend
echo   source ../venv/bin/activate
echo   python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
echo.

echo Step 3: Start Worker Process (Terminal 2)
echo   cd backend
echo   source ../venv/bin/activate
echo   python -m app.workers.simulation_worker
echo.

echo Step 4: Test Endpoint (Terminal 3)
echo   curl -X POST http://localhost:8000/queue-test
echo.

echo Expected Response:
echo   {
echo     "job_id": "...",
echo     "status": "QUEUED"
echo   }
echo.

echo For detailed instructions, see: QUEUE_SETUP_GUIDE.md
echo.
pause
