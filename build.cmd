@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

       IF NOT EXIST output MKDIR output

       REM Build and test GAZLCmd beta
       CALL tools\BuildCpp.cmd beta x64 output\GAZLCmdBeta.exe -I. GAZLCmd\GAZLCmd.cpp src\GAZL.cpp
       IF ERRORLEVEL 1 EXIT /B 1
       output\GAZLCmdBeta.exe
       IF ERRORLEVEL 1 EXIT /B 1

       REM Build Impala (release tools and demo)
       PUSHD tools
       CALL BuildImpala.bat
       IF ERRORLEVEL 1 EXIT /B 1
       POPD

       REM Copy release binary to output directory
       COPY /Y tools\GAZLCmd.exe output\GAZLCmd.exe >NUL

       REM Run Impala tests
       PUSHD impala
       PikaCmd runTests.pika
       IF ERRORLEVEL 1 EXIT /B 1
       POPD

