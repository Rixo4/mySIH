@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
.\build-cuda\silicon_patient.exe --neurons 1000 --time 200 --dt 0.02 --drug FastGPU --dose 10 --ic50_na 1000 --ic50_k 1000 --ic50_ca 1000 --hill 1.0 --connectivity 0.05 --excitatory_ratio 0.8 --external_current 3.8 --noise 0.25 --mode cuda --csv false --output output_gpu_quick
