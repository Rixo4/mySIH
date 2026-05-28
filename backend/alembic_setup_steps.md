# Alembic Setup Steps (Scientific Orchestration)

This repository currently has no Alembic config. Use these exact steps from `backend/`:

```bash
cd /home/ranjith/projectubantu/Neuro_drug_testing/backend
source /home/ranjith/projectubantu/Neuro_drug_testing/.venv/bin/activate
pip install alembic psycopg2-binary
alembic init alembic
```

Then update `alembic.ini`:

- Set `sqlalchemy.url` to your PostgreSQL DSN, or keep a placeholder and load env in `env.py`.

Update `alembic/env.py`:

```python
from app.database import Base
from app import models as _models_registry  # ensure model imports

target_metadata = Base.metadata
```

Generate and apply migration:

```bash
alembic revision -m "add simulation_jobs and dose_results" --autogenerate
alembic upgrade head
```

Expected new tables:

- `simulation_jobs`
- `dose_results`

Expected enum type (PostgreSQL):

- `simulation_job_status`
```
