# Product version: the single source for CMake, EXE metadata and ZIP names.
VERSION = 0.1

# Compatible with MSVC NMake and GNU Make. Run from the repository root.
POWERSHELL = powershell.exe -NoProfile -ExecutionPolicy Bypass

all: build

build: FORCE
	$(POWERSHELL) -File tools/build.ps1 -SkipTests

test: FORCE
	$(POWERSHELL) -File tools/build.ps1

package: FORCE
	$(POWERSHELL) -File tools/package.ps1

rebuild: FORCE
	$(POWERSHELL) -File tools/build.ps1 -Clean

# The directory named build must not prevent a rebuild.
FORCE:
