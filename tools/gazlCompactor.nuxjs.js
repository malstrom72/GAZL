load("tools/gazlTools.nuxjs.js");

if (!arguments || arguments.length < 3) {
	throw new Error("Usage: NuXJS tools/gazlCompactor.nuxjs.js input.gazl output.gazl");
}

write("" + arguments[2], compactGAZL(read("" + arguments[1])));
