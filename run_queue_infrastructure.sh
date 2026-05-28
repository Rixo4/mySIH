#!/bin/bash
# Quick start script for Queue Infrastructure
# Usage: bash run_queue_infrastructure.sh

echo "==================================="
echo "Silicon Patient Platform - Queue Infrastructure"
echo "==================================="
echo ""

# Check if running from correct directory
if [ ! -f "backend/app/main.py" ]; then
    echo "❌ Error: Please run this script from the project root directory"
    exit 1
fi

echo "✓ Environment check passed"
echo ""

# Step 1: Redis
echo "==================================="
echo "Step 1: Checking Redis"
echo "==================================="
if ! command -v redis-server &> /dev/null; then
    echo "⚠️  Redis not found. Install with: sudo apt install redis-server"
    exit 1
fi

if redis-cli ping &> /dev/null; then
    echo "✓ Redis is running on port 6379"
else
    echo "⚠️  Redis not running. Starting Redis..."
    redis-server --daemonize yes
    sleep 2
    if redis-cli ping &> /dev/null; then
        echo "✓ Redis started successfully"
    else
        echo "❌ Failed to start Redis"
        exit 1
    fi
fi
echo ""

# Step 2: Virtual Environment
echo "==================================="
echo "Step 2: Activating Virtual Environment"
echo "==================================="
if [ ! -d "venv" ]; then
    echo "❌ Virtual environment not found at ./venv"
    exit 1
fi

source venv/bin/activate
echo "✓ Virtual environment activated"
echo ""

# Step 3: Check dependencies
echo "==================================="
echo "Step 3: Checking Python Dependencies"
echo "==================================="
python -c "import redis; import rq; import fastapi" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "✓ All required packages installed"
else
    echo "❌ Missing packages. Run: pip install redis rq fastapi uvicorn"
    exit 1
fi
echo ""

# Step 4: Display instructions
echo "==================================="
echo "✓ All checks passed! Ready to run services"
echo "==================================="
echo ""
echo "To start the full queue infrastructure, open 3 terminals and run:"
echo ""
echo "Terminal 1 (FastAPI):"
echo "  cd backend && python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000"
echo ""
echo "Terminal 2 (Worker):"
echo "  cd backend && python -m app.workers.simulation_worker"
echo ""
echo "Terminal 3 (Test):"
echo "  curl -X POST http://localhost:8000/queue-test"
echo ""
echo "For detailed instructions, see: QUEUE_SETUP_GUIDE.md"
echo ""
