@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SETLOCAL

SET SOURCEDIR=%CD%
CD /D "%TEMP%"
ECHO test >PikaTest.tmp
COPY PikaTest.tmp C:\WINDOWS\ >NUL 2>NUL
IF ERRORLEVEL 1 (
	ECHO Must run with administrator right. E.g.
	ECHO.
	ECHO runas.exe /savecred /user:administrator /noprofile "CMD /C CD /D %SOURCEDIR%&&DownloadAndInstallPika.cmd"
	DEL /Q PikaTest.tmp
	EXIT /b 1
)
DEL /Q C:\WINDOWS\PikaTest.tmp
DEL /Q PikaTest.tmp

ECHO Initializing installation...

RMDIR /S /Q SourceDistribution 2>NUL

REM By Justin Godden. http://stackoverflow.com/questions/1021557/how-to-unzip-a-file-using-the-command-line
REM This script upzip's files...

    > j_unzip.vbs ECHO '
    >> j_unzip.vbs ECHO ' UnZip a file script
    >> j_unzip.vbs ECHO '
    >> j_unzip.vbs ECHO ' It's a mess, I know!!!
    >> j_unzip.vbs ECHO '
    >> j_unzip.vbs ECHO.
    >> j_unzip.vbs ECHO ' Dim ArgObj, var1, var2
    >> j_unzip.vbs ECHO Set ArgObj = WScript.Arguments
    >> j_unzip.vbs ECHO.
    >> j_unzip.vbs ECHO If (Wscript.Arguments.Count ^> 0) Then
    >> j_unzip.vbs ECHO. var1 = ArgObj(0)
    >> j_unzip.vbs ECHO Else
    >> j_unzip.vbs ECHO. var1 = ""
    >> j_unzip.vbs ECHO End if
    >> j_unzip.vbs ECHO.
    >> j_unzip.vbs ECHO If var1 = "" then
    >> j_unzip.vbs ECHO. strFileZIP = "example.zip"
    >> j_unzip.vbs ECHO Else
    >> j_unzip.vbs ECHO. strFileZIP = var1
    >> j_unzip.vbs ECHO End if
    >> j_unzip.vbs ECHO.
    >> j_unzip.vbs ECHO 'The location of the zip file.
    >> j_unzip.vbs ECHO REM Set WshShell = CreateObject("Wscript.Shell")
    >> j_unzip.vbs ECHO REM CurDir = WshShell.ExpandEnvironmentStrings("%%cd%%")
    >> j_unzip.vbs ECHO Dim sCurPath
    >> j_unzip.vbs ECHO sCurPath = CreateObject("Scripting.FileSystemObject").GetAbsolutePathName(".")
    >> j_unzip.vbs ECHO strZipFile = sCurPath ^& "\" ^& strFileZIP
    >> j_unzip.vbs ECHO 'The folder the contents should be extracted to.
    >> j_unzip.vbs ECHO outFolder = sCurPath ^& "\"
    >> j_unzip.vbs ECHO.
    >> j_unzip.vbs ECHO. WScript.Echo ( "Extracting file " ^& strFileZIP)
    >> j_unzip.vbs ECHO.
    >> j_unzip.vbs ECHO Set objShell = CreateObject( "Shell.Application" )
    >> j_unzip.vbs ECHO Set objSource = objShell.NameSpace(strZipFile).Items()
    >> j_unzip.vbs ECHO Set objTarget = objShell.NameSpace(outFolder)
    >> j_unzip.vbs ECHO intOptions = 256
    >> j_unzip.vbs ECHO objTarget.CopyHere objSource, intOptions
    >> j_unzip.vbs ECHO.
    >> j_unzip.vbs ECHO. WScript.Echo ( "Extracted." )
    >> j_unzip.vbs ECHO.

ECHO Downloading, please wait...
powershell.exe -Command "(New-Object System.Net.WebClient).DownloadFile('https://github.com/malstrom72/PikaScript/releases/latest/download/PikaCmdSourceDistribution.zip','PikaCmdSourceDistribution.zip')" || GOTO error
ECHO Extracting...
cscript /B j_unzip.vbs PikaCmdSourceDistribution.zip || GOTO error
DEL PikaCmdSourceDistribution.zip
CD SourceDistribution || GOTO error
CALL BuildPikaCmd || GOTO error
REM runas.exe /savecred /user:administrator /noprofile "CMD /K CD /D %TEMP%&&CD SourceDistribution&&InstallPika.cmd C:\WINDOWS&&CD ..&&RMDIR /S /Q SourceDistribution" || GOTO error
CALL InstallPika.cmd C:\WINDOWS || GOTO error
CD ..
RMDIR /S /Q SourceDistribution
ECHO SUCCESS!
EXIT /b 0

:error
ECHO Error %ERRORLEVEL%
EXIT /b %ERRORLEVEL%
