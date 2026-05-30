output "ecr_api_repo_url" {
  value = aws_ecr_repository.api.repository_url
}

output "ecr_worker_repo_url" {
  value = aws_ecr_repository.worker.repository_url
}

output "s3_bucket" {
  value = aws_s3_bucket.artifacts.bucket
}

output "rds_endpoint" {
  value = aws_db_instance.postgres.endpoint
}

output "redis_cluster_id" {
  value = aws_elasticache_cluster.redis.id
}

output "gpu_worker_public_ip" {
  value = aws_instance.gpu_worker.public_ip
}
