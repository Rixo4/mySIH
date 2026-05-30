terraform {
  required_providers {
    aws = { source = "hashicorp/aws", version = ">= 4.0" }
  }
}

provider "aws" {
  region = var.region
}

resource "aws_ecr_repository" "api" {
  name = var.ecr_api_name
}

resource "aws_ecr_repository" "worker" {
  name = var.ecr_worker_name
}

resource "aws_s3_bucket" "artifacts" {
  bucket = var.s3_bucket
  acl    = "private"
  force_destroy = true
}

resource "aws_db_instance" "postgres" {
  allocated_storage    = 20
  engine               = "postgres"
  engine_version       = "15"
  instance_class       = var.rds_instance_class
  name                 = var.rds_db_name
  username             = var.rds_username
  password             = var.rds_password
  skip_final_snapshot  = true
  publicly_accessible  = false
}

resource "aws_elasticache_cluster" "redis" {
  cluster_id     = var.elasticache_cluster_id
  engine         = "redis"
  node_type      = var.elasticache_node_type
  num_cache_nodes = 1
}

resource "aws_instance" "gpu_worker" {
  ami           = var.gpu_ami
  instance_type = var.gpu_instance_type
  tags = {
    Name = "gpu-worker"
  }
}
