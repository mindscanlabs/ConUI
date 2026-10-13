# ConUI skill for Claude Code

The skill teaches Claude Code how to build applications with ConUI: start a
new application from the skeleton, add ConUI to an existing program, use
ConUI as the design system of an interface in another stack, add screens and
widgets that follow the design rules, and make themes.

With the skill installed, requests like these work in any project:

- "Create a desktop app with ConUI that shows my log files in a table with a
  search field."
- "Add ConUI to this application and use it as the design system for the
  interface."
- "Restyle this web dashboard with the ConUI design language."
- "Add a settings screen with a theme picker to my ConUI app."
- "Make a ConUI theme in amber on black."

## What is in it

```
skill/
├── install.sh                  installer for macOS, Linux, Git Bash and MSYS2
├── install.ps1                 installer for Windows PowerShell
└── conui/
    ├── SKILL.md                the workflow: find the repo, pick the route, build, verify
    └── references/
        ├── api.md              the API map with working patterns
        ├── design-rules.md     the checklist every screen is checked against
        └── porting.md          the design language for non-C++ stacks (CSS tokens, components)
```

The skill is a map. The full sources and documentation stay in this
repository, and the skill reads them from there: the installer records the
repository's path in a `CONUI_HOME` file next to `SKILL.md`.

## Requirements

- [Claude Code](https://claude.com/claude-code) (CLI, desktop app or IDE
  extension).
- A local clone of this repository. Keep it: the skill points at it.
- To build ConUI applications: the toolchain from the main
  [README](../README.md#building).

## Installation

Clone the repository, then run the installer from the repository root:

```bash
git clone https://github.com/mindscanlabs/ConUI.git
cd ConUI
```

**Windows (PowerShell):**

```powershell
.\skill\install.ps1
```

If scripts are blocked by the execution policy:

```powershell
powershell -ExecutionPolicy Bypass -File .\skill\install.ps1
```

**macOS, Linux, Git Bash, MSYS2:**

```bash
./skill/install.sh
```

Both install the skill for every project, into `~/.claude/skills/conui`
(`%USERPROFILE%\.claude\skills\conui` on Windows).

**For one project only**, pass the project folder. The skill then lives in
that project's `.claude/skills/conui` and can be committed with it:

```powershell
.\skill\install.ps1 -Project C:\path\to\project
```

```bash
./skill/install.sh /path/to/project
```

**By hand:** copy the `skill/conui` folder to `~/.claude/skills/conui` and
create a file `CONUI_HOME` in it containing the absolute path of this
repository. Setting the environment variable `CONUI_HOME` to that path works
as well.

## Checking that it works

Start a new Claude Code session (skills are loaded when a session starts) and
ask:

```
What do you know about ConUI?
```

Claude should describe ConUI and mention the skill. A request that matches
the skill's description loads it automatically. It can also be requested
explicitly: "Use the conui skill to …".

## Using it

Describe the application or change in plain words. The skill makes Claude:

1. find the ConUI repository and read the relevant documents;
2. choose a route: a new application, ConUI inside an existing C++ program,
   or the design language ported to another stack;
3. sketch the screen on the cell grid and build it from ConUI widgets, with
   palette slots instead of colours and every label in the string table;
4. build with `build.sh`, which runs the self-test;
5. start the application and tell you what to check on screen.

On Windows `build.sh` closes running `ConUI.exe` and `ConUIApp.exe` processes
before building.

## Updating and removing

After `git pull`, run the installer again: it overwrites the installed copy.
To remove the skill, delete the `conui` folder from `~/.claude/skills/` (or
from the project's `.claude/skills/`).
