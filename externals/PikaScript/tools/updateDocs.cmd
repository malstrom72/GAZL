CD /D "%~dp0\.."
output\PikaCmd.exe tools\ExportHelp.pika
output\PikaCmd.exe tools\UpdateHtmlDox.pika
where doxygen >nul 2>nul
IF %ERRORLEVEL% EQU 0 (
    doxygen docs\PikaScriptDoxyfile
    CD docs\latex
    pdflatex refman.tex
    CD ..
    move latex\refman.pdf PikaScript.pdf
) ELSE (
    echo doxygen not found, skipping PDF build 1>&2
)
