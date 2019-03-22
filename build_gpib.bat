@echo off
rem  下载安装mingw https://osdn.net/projects/mingw/
set  PATH=C:\MinGW\bin;C:\MinGW\mingw32\bin;%PATH%
rem del gpib.exe
rem set  PATH=%USERPROFILE%\Apps\PortableApps/MinGW/bin;%USERPROFILE%\Apps\PortableApps\MinGW/mingw32/bin;%PATH%
rem set  PATH=%USERPROFILE%\Apps\PortableApps\MinGW\bin;%USERPROFILE%\Apps\PortableApps\MinGW\mingw32\bin;%PATH%
rem g++ 路径设置中不要带双引号，否则出现 g++: CreateProcess: No such file or directory    ,但路径中 \ 和 / 可以混用
rem set  PATH="%USERPROFILE%\Apps\PortableApps\MinGW\bin";"%USERPROFILE%\Apps\PortableApps\MinGW\mingw32\bin";%PATH%
g++ -fpermissive -o GPIB.exe -I .\ni .\ni\gpib-32.obj GPIB.c