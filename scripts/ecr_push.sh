#!/usr/bin/env bash
set -euo pipefail

# Usage: ./scripts/ecr_push.sh <aws_account_id> [region]
AWS_ACCOUNT="${1:-${AWS_ACCOUNT:-}}"
if [ -z "$AWS_ACCOUNT" ]; then
	# Try to discover account id from AWS CLI
	AWS_ACCOUNT=$(aws sts get-caller-identity --query Account --output text 2>/dev/null || true)
fi
if [ -z "$AWS_ACCOUNT" ]; then
	echo "AWS account id not provided. Pass it as the first arg or set the AWS_ACCOUNT env var." >&2
	exit 1
fi

AWS_REGION="${2:-${AWS_REGION:-us-east-1}}"

REPO_API=silicon-patient-api
REPO_WORKER=silicon-patient-worker

ECR_API="${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com/${REPO_API}"
ECR_WORKER="${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com/${REPO_WORKER}"

echo "Creating ECR repositories (if not exist)..."
aws ecr create-repository --repository-name ${REPO_API} --region ${AWS_REGION} 2>/dev/null || true
aws ecr create-repository --repository-name ${REPO_WORKER} --region ${AWS_REGION} 2>/dev/null || true

echo "Logging in to ECR..."
aws ecr get-login-password --region ${AWS_REGION} | docker login --username AWS --password-stdin ${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com

echo "Building and pushing ${REPO_API}..."
docker build -t ${REPO_API} -f backend/Dockerfile .
docker tag ${REPO_API}:latest ${ECR_API}:latest
docker push ${ECR_API}:latest

echo "Building and pushing ${REPO_WORKER}..."
docker build -t ${REPO_WORKER} -f backend/Dockerfile.worker .
docker tag ${REPO_WORKER}:latest ${ECR_WORKER}:latest
docker push ${ECR_WORKER}:latest

echo "Done. API: ${ECR_API}:latest, WORKER: ${ECR_WORKER}:latest"
