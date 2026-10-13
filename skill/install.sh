#!/usr/bin/env bash
# Installs the ConUI skill for Claude Code.
#
#     ./skill/install.sh              for every project: ~/.claude/skills/conui
#     ./skill/install.sh <project>    for one project:   <project>/.claude/skills/conui
#
# The skill is copied, and the absolute path of this repository is written to
# CONUI_HOME next to SKILL.md, so the skill can find the sources and docs.
# Run it again after updating the repository.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"

# Claude Code on Windows reads the Windows profile, not the MSYS/Git Bash $HOME.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        user_home="$(cygpath -u "$USERPROFILE")"
        repo_record="$(cygpath -w "$repo")"
        ;;
    *)
        user_home="$HOME"
        repo_record="$repo"
        ;;
esac

if [ $# -ge 1 ]; then
    dest="$(cd "$1" && pwd)/.claude/skills/conui"
else
    dest="$user_home/.claude/skills/conui"
fi

mkdir -p "$dest"
cp -R "$here/conui/." "$dest/"
printf '%s\n' "$repo_record" > "$dest/CONUI_HOME"

echo "ConUI skill installed: $dest"
echo "Repository recorded:   $repo_record"
echo "Start a new Claude Code session to load it."
