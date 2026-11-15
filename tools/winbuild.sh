#!/bin/bash
# Sprocket Rally: build on a Windows PC over SSH (kormake, MSVC and the Vulkan SDK there).
#
# Where that PC is and how to log in come from a file of shell variables that is NOT in
# the repository: $SPROCKET_WIN_ENV, or ~/.config/sprocket-winbuild.env. It sets
#   SPROCKET_WIN_HOST   the PC's address
#   SPROCKET_WIN_ROOT   the project there, e.g. 'C:\Users\you\games\sprocket'
#   PC_USER, PC_PASSWORD  the SSH login (it may source another file that holds them)
#   SPROCKET_WIN_TASK_USER  the desktop user the smoke run starts as (default: PC_USER)
# The password is never printed or put on a command line (sshpass reads it from the environment).
#
#   winbuild.sh sync            copy the game's source, shaders and module file to the PC
#   winbuild.sh build           run `kormake sprocket ship` there (slangc on PATH)
#   winbuild.sh cmd '<command>' run one cmd.exe command there, from the project folder
#   winbuild.sh package         ship build, package, bundle DLLs and readme, zip
#   winbuild.sh smoke           run the staged package 8 s on the PC's desktop, report
#   winbuild.sh get <remote> <local>   copy one file back
#   winbuild.sh put <local> <remote>   copy one file over
set -u
LOCAL=$(cd "$(dirname "$0")/.." && pwd)
set -a; . "${SPROCKET_WIN_ENV:-$HOME/.config/sprocket-winbuild.env}" >/dev/null 2>&1; set +a
for v in SPROCKET_WIN_HOST SPROCKET_WIN_ROOT PC_USER PC_PASSWORD; do
    if [ -z "${!v:-}" ]; then echo "$v is not set (see the top of this script)" >&2; exit 2; fi
done
HOST=$SPROCKET_WIN_HOST
REMOTE=$SPROCKET_WIN_ROOT
REMOTE_SCP=${REMOTE//\\//}
TASK_USER=${SPROCKET_WIN_TASK_USER:-$PC_USER}
export SSHPASS="$PC_PASSWORD"
OPTS=(-o StrictHostKeyChecking=accept-new -o ConnectTimeout=15)
rsh() { sshpass -e ssh "${OPTS[@]}" "$PC_USER@$HOST" "$@"; }

case "${1:-}" in
sync)
    sshpass -e scp "${OPTS[@]}" "$LOCAL"/sprocket/src/*.cc "$LOCAL"/sprocket/src/*.h "$PC_USER@$HOST:$REMOTE_SCP/sprocket/src/" &&
    sshpass -e scp "${OPTS[@]}" "$LOCAL"/sprocket/shaders/* "$PC_USER@$HOST:$REMOTE_SCP/sprocket/shaders/" &&
    sshpass -e scp "${OPTS[@]}" "$LOCAL"/sprocket/module.lua "$PC_USER@$HOST:$REMOTE_SCP/sprocket/" &&
    sshpass -e scp "${OPTS[@]}" "$LOCAL"/README.md "$PC_USER@$HOST:$REMOTE_SCP/" &&
    rsh "cmd /c \"if not exist $REMOTE\\sprocket\\tools mkdir $REMOTE\\sprocket\\tools\"" &&
    sshpass -e scp "${OPTS[@]}" "$LOCAL"/sprocket/tools/sprocket.ico "$PC_USER@$HOST:$REMOTE_SCP/sprocket/tools/" &&
    echo "synced"
    ;;
build)
    rsh "cmd /c \"cd /d $REMOTE && set PATH=C:\\VulkanSDK\\1.4.363.1\\Bin;%PATH% && ..\\korvid\\tools\\kormake\\bin\\windows\\kormake.exe sprocket ship\"" 2>&1
    ;;
cmd)
    shift
    rsh "cmd /c \"cd /d $REMOTE && set PATH=C:\\VulkanSDK\\1.4.363.1\\Bin;%PATH% && $*\"" 2>&1
    ;;
package)
    # ship build, the engine's packager, then the folder friends get: the game, SDL3 and the
    # VC runtime beside it, shader sources, licences, a readme; zipped
    VCR='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT'
    KM='..\korvid\tools\kormake\bin\windows\kormake.exe'
    D='dist\stage\sprocket-rally'   # staged, so a copy of the game that is running is never pulled apart
    run() { rsh "cmd /c \"cd /d $REMOTE && set PATH=C:\\VulkanSDK\\1.4.363.1\\Bin;%PATH% && $*\"" 2>&1; }
    # the exe's icon: a resource compiled here and handed to the linker through its LINK variable,
    # along with the windowed subsystem, so no console window opens beside the game
    RC=$(run 'where /r "C:\Program Files (x86)\Windows Kits\10\bin" rc.exe' | tr -d '\r' | grep -i '\\x64\\rc.exe' | tail -1)
    printf '1 ICON "sprocket.ico"\r\n' > /tmp/sprocket.rc
    sshpass -e scp "${OPTS[@]}" /tmp/sprocket.rc "$PC_USER@$HOST:$REMOTE_SCP/sprocket/tools/sprocket.rc"; rm -f /tmp/sprocket.rc
    run "cd sprocket\\tools & \"$RC\" /nologo /fo sprocket.res sprocket.rc" | tail -2
    run "if exist build\\windows-ship\\bin\\sprocket.exe del build\\windows-ship\\bin\\sprocket.exe" >/dev/null
    run "set LINK=$REMOTE\\sprocket\\tools\\sprocket.res /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup& $KM sprocket ship" | grep -i 'error\|warning\|LINK' | head -20
    run "$KM package sprocket ship" | tail -1
    run "if exist $D rmdir /s /q $D" >/dev/null
    run "xcopy /e /i /q /y dist\\sprocket $D" | tail -1
    run "copy /y build\\windows-ship\\bin\\SDL3.dll $D\\" >/dev/null
    run "copy /y \"$VCR\\vcruntime140.dll\" $D\\" >/dev/null
    run "copy /y \"$VCR\\vcruntime140_1.dll\" $D\\" >/dev/null
    run "mkdir $D\\shaders-src & copy /y sprocket\\shaders\\*.slang* $D\\shaders-src\\" >/dev/null
    run "copy /y ..\\korvid\\vendor\\SDL3-3.4.20\\LICENSE.txt $D\\licenses\\SDL3-LICENSE.txt" >/dev/null
    sshpass -e scp "${OPTS[@]}" "$LOCAL/tools/README-windows.txt" "$PC_USER@$HOST:$REMOTE_SCP/dist/stage/sprocket-rally/README.txt"
    run "cd dist & if exist sprocket-rally-windows-x64.zip del sprocket-rally-windows-x64.zip" >/dev/null
    run "cd dist\\stage & tar -a -c -f ..\\sprocket-rally-windows-x64.zip sprocket-rally" >/dev/null
    # the playable folder: brought up to date from the stage (the exe stays as it is while it is running)
    run "robocopy $D dist\\sprocket-rally /mir /njh /njs /ndl /nfl /r:0 /w:0 & if errorlevel 8 echo NOTE: some files in dist\\sprocket-rally are in use and were not replaced (close the game and package again)" | tr -d '\r' | grep -v '^\s*$' | tail -4
    run "dir /b dist\\sprocket-rally & dir dist\\*.zip" | tr -d '\r' | tail -18
    ;;
smoke)
    # run the staged package for 8 s in the logged-in desktop session (SSH has no desktop)
    printf '@echo off\r\ncd /d %s\\dist\\stage\\sprocket-rally\r\nsprocket.exe --run-seconds 8 > %s\\dist\\smoke.log 2>&1\r\necho EXIT=%%ERRORLEVEL%% >> %s\\dist\\smoke.log\r\n' "$REMOTE" "$REMOTE" "$REMOTE" > /tmp/sprocket-smoke.bat
    sshpass -e scp "${OPTS[@]}" /tmp/sprocket-smoke.bat "$PC_USER@$HOST:$REMOTE_SCP/dist/smoke.bat"; rm -f /tmp/sprocket-smoke.bat
    rsh "schtasks /create /f /tn SprocketSmoke /tr $REMOTE\\dist\\smoke.bat /sc once /st 23:59 /it /ru $TASK_USER" | tail -1
    rsh 'schtasks /run /tn SprocketSmoke' | tail -1
    sleep 14
    rsh 'schtasks /delete /f /tn SprocketSmoke' >/dev/null
    rsh "cmd /c \"findstr /i \"ERROR EXIT frames\" $REMOTE\\dist\\smoke.log & del $REMOTE\\dist\\smoke.bat\"" | tr -d '\r' | cut -c1-200
    ;;
put)
    sshpass -e scp "${OPTS[@]}" "$2" "$PC_USER@$HOST:$3"
    ;;
get)
    sshpass -e scp "${OPTS[@]}" "$PC_USER@$HOST:$2" "$3"
    ;;
*)
    echo "usage: winbuild.sh sync | build | cmd '<command>' | get <remote> <local>" >&2; exit 2
    ;;
esac
