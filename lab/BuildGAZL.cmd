call iclvars
del *.dyn
del *.dpi
del *.obj
icl @IntelRelease.cfg src\GAZL.cpp GAZLTest.cpp /Qprof-gen /Febuild\Windows\ICC\GAZLpgo.exe
build\Windows\ICC\GAZLpgo unittest.gazl test
rem build\Windows\ICC\GAZLpgo perftests.gazl main DO_FUNC_TEST 1 DO_INSTRUCTION_TEST 1 DO_CODE_TEST 1
icl @IntelRelease.cfg /FAcs src\GAZL.cpp GAZLTest.cpp /Qprof-use /Febuild\Windows\ICC\GAZLpgo.exe
del *.dyn
del *.dpi
del *.obj
icl @IntelRelease.cfg /FAcs src\GAZL.cpp GAZLTest.cpp /Febuild\Windows\ICC\GAZL.exe
