if (typeof impalaCompiler !== "function") {
	throw new Error("impalaCompiler is not available");
}

var nuxjsSmokeOutput = [];
var nuxjsSmokeResult = impalaCompiler("function main()\nlocals int x\n{\n}\n", {
	output: function (line) {
		nuxjsSmokeOutput[nuxjsSmokeOutput.length] = line;
	},
	randomId: 0xabcdef,
	sourceName: "nuxjs-smoke.impala",
});

if (!nuxjsSmokeResult || !nuxjsSmokeResult[0]) {
	throw new Error("impalaCompiler returned failure in NuXJS smoke test");
}

if (nuxjsSmokeOutput.length === 0) {
	throw new Error("impalaCompiler did not emit output in NuXJS smoke test");
}
