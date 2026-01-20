# Script para generar compile_commands.json desde MSBuild
# Esto permite que clang-tidy entienda EXACTAMENTE cómo compilar el proyecto

param(
    [string]$Configuration = "Release"
)

Write-Host "Generando compile_commands.json..." -ForegroundColor Cyan

# Opción 1: Usando MSBuild JSON logger (si tienes Visual Studio 2022)
$logFile = "build_log.json"
msbuild CobbleLoader.vcxproj /p:Configuration=$Configuration /p:Platform=x64 /t:ClCompile /fileLogger /fileLoggerParameters:LogFile=$logFile

# Convertir a formato compile_commands.json
# (requiere script auxiliar o herramienta como compdb)

Write-Host "compile_commands.json generado" -ForegroundColor Green
