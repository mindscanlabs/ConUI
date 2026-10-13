# Installs the ConUI skill for Claude Code (Windows PowerShell).
#
#     .\skill\install.ps1                    for every project: %USERPROFILE%\.claude\skills\conui
#     .\skill\install.ps1 -Project <path>    for one project:   <path>\.claude\skills\conui
#
# The skill is copied, and the absolute path of this repository is written to
# CONUI_HOME next to SKILL.md, so the skill can find the sources and docs.
# Run it again after updating the repository.
param([string]$Project)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $here '..')).Path

if ($Project) {
    $dest = Join-Path (Resolve-Path $Project).Path '.claude\skills\conui'
} else {
    $dest = Join-Path $env:USERPROFILE '.claude\skills\conui'
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Path (Join-Path $here 'conui\*') -Destination $dest -Recurse -Force
[System.IO.File]::WriteAllText((Join-Path $dest 'CONUI_HOME'), "$repo`n", (New-Object System.Text.UTF8Encoding $false))

Write-Output "ConUI skill installed: $dest"
Write-Output "Repository recorded:   $repo"
Write-Output "Start a new Claude Code session to load it."
