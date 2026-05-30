variable "region" {
  type    = string
  default = "us-east-1"
}

variable "ecr_api_name" {
  type    = string
  default = "silicon-patient-api"
}

variable "ecr_worker_name" {
  type    = string
  default = "silicon-patient-worker"
}

variable "s3_bucket" {
  type = string
}

variable "rds_instance_class" {
  type    = string
  default = "db.t3.medium"
}

variable "rds_db_name" {
  type    = string
  default = "silicon_patient"
}

variable "rds_username" {
  type = string
}

variable "rds_password" {
  type = string
  sensitive = true
}

variable "elasticache_cluster_id" {
  type    = string
  default = "silicon-patient-redis"
}

variable "elasticache_node_type" {
  type    = string
  default = "cache.t3.micro"
}

variable "gpu_ami" {
  type = string
}

variable "gpu_instance_type" {
  type    = string
  default = "g5.xlarge"
}
