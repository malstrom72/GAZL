load("tools/gazlTools.nuxjs.js");

var updateUnitTestSource = read("src/UnitTest.gazl");
var updateUnitTestCompact = compactGAZL(updateUnitTestSource);
var updateUnitTestCode = "const char* UNITTEST = \n" + textToCpp(updateUnitTestCompact) + ";\n";

write("src/UnitTest.inc", updateUnitTestCode);
print("Updated UnitTest.inc");
