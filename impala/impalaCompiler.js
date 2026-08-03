/* GENERATED from impala/impala.jspeg by `node impala/updateJSPEG.js` -- do not edit by hand. */
var $$parser = {};
var impalaCompilerImpl = (function(_s, _options) {
var _hostOptions = _options || {};
var KEYWORD_WORDS = [
	'abs', 'array', 'assert', 'case', 'const', 'copy', 'default', 'do', 'else', 'export', 'extern',
	'float', 'floor', 'for', 'from', 'ftoi', 'funcptr', 'functype', 'function', 'global', 'goto', 'if',
	'import', 'inline', 'int', 'itof', 'locals', 'loop', 'native', 'null', 'nullfunc', 'pointer', 'readonly',
	'returns', 'sizeof', 'struct', 'switch', 'temporary', 'to', 'while'
];
var output = (typeof _hostOptions.output === 'function') ? _hostOptions.output : function () {};
var hostRandomId = Object.prototype.hasOwnProperty.call(_hostOptions, 'randomId')
	? _hostOptions.randomId
	: undefined;
$$parser.sourceName = Object.prototype.hasOwnProperty.call(_hostOptions, 'sourceName')
	? _hostOptions.sourceName
	: undefined;
{
    /**
     * map(target, k1, v1, k2, v2, ...)
     *   assigns target[k1]=v1, etc.
     */
    function map(target /*, k1, v1, ... */) {
        for (var i = 1; i + 1 < arguments.length; i += 2) {
            target[ arguments[i] ] = arguments[i+1];
        }
        return target;
    }

    /**
     * Deep-clone an object or array (only handles plain objects & arrays)
     */
    function clone(o) {
        // primitives, functions, null
        if (typeof o !== 'object' || o === null) {
            return o;
        }
        // array vs object
        var r = (o instanceof Array) ? [] : {};
        for (var k in o) {
            // only own props (in ES3 all enumerable are “own” unless on prototype)
            var v = o[k];
            r[k] = (typeof v === 'object' && v !== null ? clone(v) : v);
        }
        return r;
    }


    /** template baker: "Hello {name}" -> eval expressions in {...} */
    function bake(str) {
        // same pattern as /\{([^}]+)\}/g  but expressed as a string because poor jspeg parsing
        var re = new RegExp("\\{([^}]+)\\}", "g");
        return str.replace(re, function (_, expr) {
            return eval(expr);
        });
    }

    /** simple assertion */
    function assert(cond, msg) {
        if (!cond) throw new Error("Assertion failed" + (msg ? ": " + msg : ""));
    }

    /** turn arguments into a real Array (optional from-index) */
    function args(src, from) {
        var start = (arguments.length > 1 ? from : 0);
        var a = [];
        for (var i = start; i < src.length; i++) {
            a.push(src[i]);
        }
        return a;
    }

    /** string utilities */
    function replace(str, sub, by) {
        return str.split(sub).join(by);
    }
    function find(str, chars) {
        var i = 0;
        while (i < str.length && chars.indexOf(str[i]) < 0) {
            i++;
        }
        return i;
    }
    function span(str, chars) {
        var i = 0;
        while (i < str.length && chars.indexOf(str[i]) >= 0) i++;
        return i;
    }
    function rspan(str, chars) {
        var i = str.length;
        while (i > 0 && chars.indexOf(str[i-1]) >= 0) i--;
        return i;
    }
    function char(code) {
        return String.fromCharCode(code & 0xFF);
    }
    function ordinal(ch) {
        return ((ch + "").charCodeAt(0)) & 0xFF;
    }

    function evaluate(str) {
        return JSON.parse(str);
    }

    /** queue ops on plain Arrays */
    function resetQueue(q) {
        q.length = 0;
    }
    function queueSize(q) {
        return q.length;
    }
    function pushBack(q, v) {
        q.push(v);
    }
    function popBack(q) {
        if (q.length === 0) throw new Error("Queue underrun");
        return q.pop();
    }
    function pushFront(q, v) {
        q.unshift(v);
    }
    function popFront(q) {
        if (q.length === 0) throw new Error("Queue underrun");
        return q.shift();
    }

    /** math/random helpers */
    function random() {
        return Math.random();
    }
    function floor(x) {
        return Math.floor(x);
    }
    function time() {
        return (new Date()).getTime();
    }


    /* --------------------------------
     *  Impala-JSPEG  ▸  core tables / helpers  (ES3)
     *  arrays now rely on .length / .push
     * -------------------------------- */

    /* 1  constants & simple flags */
    var IMPALA_VERSION = '1.0';
    var dry            = false;
    var legacyMode     = (typeof _hostOptions !== 'undefined' && _hostOptions != null
            && !!_hostOptions.legacy);                          /// `--legacy` downgrades strict-expression errors to warnings
    var rangeChecks    = (typeof _hostOptions !== 'undefined' && _hostOptions != null
            && !!_hostOptions.rangeChecks);                     /// `--range-checks` emits DEBUG-gated runtime bounds tests
    var units          = ((typeof _hostOptions !== 'undefined' && _hostOptions != null
            && _hostOptions.units) || undefined);               /// import-closure spans {name,start,end}[], for origins
    var declOffset     = 0;                            /// offset of the declaration being parsed - see `root`
    var declSource     = '';                           /// its source text, so deferred checks can still render a caret

    /* 2  make sure the buckets exist */
    var META_TO_GAZL   = {};
    var SUPPORTED_OPS  = {};
    var CASTS_TO_TYPES = {};
    var ZEROES = {};
    var TYPE_SUFFIXES  = {};
    var VERBOSE_TYPES  = {};
    var sourceName = undefined;
    var metacode = [];
    var strings = { s:[], a:[] };
    var labelCounter = 0;
    var stock = { '%': [], '<': [] };
    var counters = { '%': 0,  '<': 0  };
    var symbols = { 'locals': {}, 'globals': {}, 'functions': {}, 'defines': {} };
    var structs = {};                                    /// name -> { fields:[{name,type,elem,offset,words}], words, complete }
    var openStruct = undefined;                          /// struct whose field list is being parsed (its fields own their extent scratches)
    var functypes = {};                                  /// name -> signature { params, returnList, returnCount, returns, returnWords, complete }
    var topNames = {};                                   /// name -> kind ('global'/'function'/'const'/'struct'/'functype'), one flat namespace
    var guardCounter = 0;                                /// mints `.g<N>` skip labels for deferred assertions; NOT labelCounter, which resets per function
    var emittedGuards = {};                              /// (array, index) pairs already asserted in this function - the same assertion twice says nothing new
    var initElemType = undefined;                        /// declared element type an `InitList` must match ('i'/'f'/'p'); undefined = untyped array, accept what comes
    var switchStack = [];
    var noForward = false;
    var exportNext = false;                              /// set while parsing an `export`-prefixed declaration (Step 5)

    /* 3  bulk-fill the lookup tables */
    map(META_TO_GAZL,
        '=',   'MOV?', ':=',  'MOV?',
        '=itof','iTOf', '=ftoi','fTOi', '=abs','ABS?', '=floor','FLOf',
        '=[]','PEEK',  '[]=','POKE',   '=[]$','GETL',  '[]$=','SETL',
        '=*','PEEK', ':=*','PEEK', '*=','POKE', '=&','ADRL', 'copy','COPY',
        '-->','GOTO', '-->#','SWCH',  '...','FOR?',  '()','CALL', '--^','RETU', 'FAIL','FAIL',
        '|','IOR?', '&','AND?', '^','XOR?', '<<','SHL?', '>>>','SHRu', '>>','SHR?',
        '+','ADD?', '-','SUB?', '*','MUL?', '/','DIV?', '%','MOD?', 'd','DIFp',
        '<=','LEQ?', '<','LSS?', '>=','GEQ?', '>','GRT?', '!=','NEQ?', '==','EQU?',
        '!<=','GRT?','!<','GEQ?','!>=','LSS?','!>','LEQ?','!!=','EQU?','!==','NEQ?'
    );

    map(SUPPORTED_OPS,
        '=*p','?', '=&f','p', '=&F','p', '=&i','p', '=&p','p', '=&?','p',
        '=-i','i', '=-f','f', '=~i','i',
        '=floatf','f', '=float?','f', '=funcptrF','F', '=funcptr?','F',
        '=inti','i', '=int?','i', '=pointerp','p', '=pointer?','p',
        '=absi','i', '=absf','f', '=itofi','f', '=ftoif','i', '=floorf','f',
        '|ii','i', '&ii','i', '^ii','i', '<<ii','i', '>>>ii','i', '>>ii','i',
        '+ii','i', '-ii','i', '*ii','i', '/ii','i', '%ii','i',
        '+ff','f', '-ff','f', '*ff','f', '/ff','f',
        '+pi','p', '-pi','p', '-pp','i',
        '=[]pi','?',
        '<=ii','i', '<ii','i', '>=ii','i', '>ii','i', '!=ii','i', '==ii','i',
        '<=ff','f', '<ff','f', '>=ff','f', '>ff','f', '!=ff','f', '==ff','f',
        '<=pp','p', '<pp','p', '>=pp','p', '>pp','p', '!=pp','p', '==pp','p',
        '<=FF','F', '<FF','F', '>=FF','F', '>FF','F', '!=FF','F', '==FF','F'
    );

    map(CASTS_TO_TYPES, 'float','f','funcptr','F','int','i','pointer','p');
    map(ZEROES,         'f','#0.0','i','#0','p','&NULL','F','&NULL');
    /* GAZL 2: `'F','p'` is where Impala THROWS AWAY the funcptr/data-pointer distinction it already has -
       GAZL 1 offers only `p` for both, so a funcptr array and a pointer array emit identical `DATp` rows.
       When GAZL 2 lands the `t` (target) type this becomes `'F','t'` and Impala needs nothing else; the
       type is already tracked. See docs/GAZL2FunctionPointers.md. */
    map(TYPE_SUFFIXES,  'void','', 'i','i','f','f','p','p','F','p','U','',
                                 'N','',   'A','A','?','E', 'V','');
    map(VERBOSE_TYPES,  'i','int','f','float','p','pointer','F','funcptr',
                                 'U','function','N','native','A','array','S','struct','?','untyped',
                                 'V','void');

    function signatureParamCategory(type) {
        switch (type) {
            case 'i': return 'int';
            case 'f': return 'float';
            case 'p': return 'ptr';
            case 'F': return 'funcptr';
            default:  return 'unknown';
        }
    }

    /* render a full element descriptor as a single metadata token: 'p:i' -> "int-ptr",
       'p:p:i' -> "int-ptr-ptr", 'i' -> "int", 'p' -> "ptr" (element-unknown pointer) */
    function signatureCategoryForDesc(desc) {                     /// 'p:i'->"int-ptr", 'p:Filter'->"Filter-ptr"
        if (desc === undefined) {
            return 'unknown';
        }
        var head = descHead(desc);
        var tail = descTail(desc);
        if (head === 'S' && tail !== undefined) {                 /* struct-value marker (`S:name`) - the name is the tail.
                                                                     The marker ALWAYS carries a tail, so a bare `S` here is a
                                                                     struct NAMED "S", not a marker - fall through to resolve it
                                                                     as a name, else it (and `S pointer`) render "unknown". */
            return signatureCategoryForDesc(tail);
        }
        if (head === 'F') {                                       /* funcptr: the named type, or bare "funcptr" */
            return (tail !== undefined ? tail : 'funcptr');
        }
        if (tail === undefined) {
            return ((isStructAtom(head) || isFuncTypeAtom(head))
                    ? head : signatureParamCategory(head));
        }
        return signatureCategoryForDesc(tail) + '-ptr';           /* head is a pointer level */
    }

    /* full descriptor of a declared symbol: type char + optional element chain */
    function fullDescFor(type, elem) {
        return (elem !== undefined ? type + ':' + elem : type);
    }

    function signatureReturnCategory(type, known) {
        switch (type) {
            case 'i': return 'int';
            case 'f': return 'float';
            case 'p': return 'ptr';
            case 'F': return 'funcptr';
            case 'V': return 'void';
            case '?': return (known ? 'void' : 'unknown');
            default:  return 'unknown';
        }
    }

    function ensureFunctionSignature(name) {
        if (!name) {
            return undefined;
        }

        if (!symbols || !symbols.functions) {
            return undefined;
        }

        var entry = symbols.functions[name];
        if (!entry) {
            return undefined;
        }

        if (!entry.signature) {
            entry.signature = {};
        }

        return entry.signature;
    }

    function isConcreteType(type) {
        return type !== undefined && type !== '?';
    }

    function renderParamList(params) {
        if (!params || params.length === 0) {
            return '';
        }

        var parts = [];
        for (var idx = 0; idx < params.length; ++idx) {
            var param = params[idx] || {};
            var typeName = (param.type === 'S'
                    ? (param.struct || 'unknown')                 /* by-value struct param -> the struct name */
                    : (param.elem !== undefined
                        ? signatureCategoryForDesc(fullDescFor(param.type, param.elem))
                        : signatureParamCategory(param.type)));
            var name = param.name;
            if (!name) {
                name = 'arg' + idx;
            }
            parts.push(typeName + ' ' + name);
        }
        return parts.join(', ');
    }

    function renderTypeList(typeCodes, elems) {
        if (!typeCodes || typeCodes.length === 0) {
            return '';
        }

        var parts = [];
        for (var i = 0; i < typeCodes.length; ++i) {
            parts.push(typeCodes[i] === 'S' && elems && elems[i] !== undefined
                    ? elems[i]                              /* by-value struct: the struct name itself */
                    : signatureCategoryForDesc(fullDescFor(typeCodes[i], elems ? elems[i] : undefined)));
        }
        return parts.join(', ');
    }

    /* Types only, no names: what a signature CLAIM actually asserts, and the only part two claims
       about one name have to agree on. Element chains stay in - gazl-validate treats a bare `ptr` as
       element-UNKNOWN and lets it match any chain, so flattening `V-ptr` to `ptr` would silently
       disarm the cross-unit check. That is the only type record a name-only `extern function f` has. */
    function renderParamTypes(params) {
        var parts = [], count = (params ? params.length : 0);
        for (var idx = 0; idx < count; ++idx) {
            var param = params[idx] || {};
            parts.push(param.type === 'S' ? (param.struct || 'unknown')
                    : signatureCategoryForDesc(fullDescFor(param.type, param.elem)));
        }
        return parts.join(', ');
    }

    /* The comparable shape of anything callable - a function signature, an extern prototype or a
       `functype`, which all carry the same params/returns fields. */
    function signatureShape(name, sig) {
        return name + '(' + renderParamTypes(sig.params) + ') -> ' + renderReturnType(sig);
    }

    /* Two claims about one name that do not agree - `got` arrived second. A DEFINITION outranks a
       declaration, so it is named as authoritative where the closure has one; with none in sight the
       claims are only judgeable against each other, and blaming a definition would cite no file. */
    function failDisagreement(what, got, had, code, priorIsDefinition, noun, plural, sourceCode, sourceOffset) {
        if (priorIsDefinition) {
            fail('extern declaration of ' + what + ' does not match its definition: declared "'
                    + got + '" but definition provides "' + had + '"', sourceCode, sourceOffset, code,
                    'the definition is authoritative - correct the declaration, or drop it and let the definition speak');
        }
        fail('extern declarations of ' + what + ' disagree: "' + got + '" vs "' + had + '"',
                sourceCode, sourceOffset, code,
                'one ' + noun + ' cannot have two ' + plural
                        + ' - make the declarations identical, or keep only one');
    }

    function renderReturnType(signature) {
        if (signature.returnCount > 1) {                           /* multiple return values -> (t1, t2, ...) */
            var parts = [];
            for (var idx = 0; idx < signature.returnList.length; ++idx) {
                var r = signature.returnList[idx];
                parts.push(r.elem !== undefined ? signatureCategoryForDesc(fullDescFor(r.type, r.elem))
                        : signatureParamCategory(r.type));
            }
            return '(' + parts.join(', ') + ')';
        }
        if (signature.returns === 'S') {                           /* by-value struct return -> the struct name */
            return (signature.returnStruct || 'unknown');
        }
        return (signature.returnElem !== undefined
                ? signatureCategoryForDesc(fullDescFor(signature.returns, signature.returnElem))
                : signatureReturnCategory(signature.returns, signature.returns !== undefined));
    }

    /* Compiling an import closure means ONE concatenated source, so a raw offset gives a line that
       indexes the concatenation and a name that is always the root unit - the wrong file, on the
       wrong line, for everything past the first. The spans recorded by the closure walk map the
       offset back to the unit that owns it. A lone unit is left exactly as it was: no name, no
       shift, so a source that imports nothing keeps emitting the bytes it always did. */
    function originUnit(offset) {                                 /* no local alias: `var units = units` rewrites to `var units = units` */
        if (!units || units.length <= 1) {
            return undefined;
        }
        for (var i = 0; i < units.length; ++i) {
            if (offset >= units[i].start && offset <= units[i].end) {
                return units[i];
            }
        }
        return undefined;
    }

    function computeOrigin(sourceName, sourceCode, sourceOffset) {
        if (!sourceCode || sourceOffset == null) {
            return undefined;
        }

        var offset = sourceOffset;
        if (offset < 0) {
            return undefined;
        }
        if (offset > sourceCode.length) {
            offset = sourceCode.length;
        }

        var unit = originUnit(offset);
        var line = 1;
        var column = 1;
        for (var idx = (unit ? unit.start : 0); idx < offset; ++idx) {
            var ch = sourceCode.charAt(idx);
            if (ch === '\r') {
                if (idx + 1 < sourceCode.length && sourceCode.charAt(idx + 1) === '\n') {
                    idx += 1;
                }
                line += 1;
                column = 1;
                continue;
            }
            if (ch === '\n') {
                line += 1;
                column = 1;
                continue;
            }
            column += 1;
        }

        var origin = line + ':' + column;
        var name = (unit ? unit.name : sourceName);
        if (name) {
            origin = name + ':' + origin;
        }
        return origin;
    }

    function appendOrigin(comment, sourceName, sourceCode, sourceOffset) {
        if (!comment) {
            return comment;
        }

        var origin = computeOrigin(sourceName, sourceCode, sourceOffset);
        if (origin) {
            return comment + ' @ ' + origin;
        }
        return comment;
    }

    formatFunctionSignatureComment = function (name, signature, role,
                                                         sourceName, sourceCode, sourceOffset) {
        if (!signature) {
            return undefined;
        }

        var paramsText = renderParamList(signature.params);
        var returnType = renderReturnType(signature);
        var kind = (role ? role : 'func');
        if (exportNext) kind = 'export ' + kind;         /* Step 5: host-visible marker */

        var originName = (sourceName !== undefined ? sourceName : (signature ? signature.sourceName : undefined));
        var originCode = (sourceCode !== undefined ? sourceCode : (signature ? signature.sourceCode : undefined));
        var originOffset = (sourceOffset !== undefined ? sourceOffset : (signature ? signature.sourceOffset : undefined));

        return appendOrigin('signature ' + kind + ' ' + name + '(' + paramsText + ') -> ' + returnType,
                            originName, originCode, originOffset);
    };

    formatCallExpectationComment = function (name, signature, actualTypes, callResultType,
                                                      sourceName, sourceCode, sourceOffset, actualElems) {
        var label = (name || 'function');
        var paramsText;
        var hasSignature = !!(signature && signature.params);

        paramsText = (hasSignature ? renderParamTypes(signature.params)
                                   : renderTypeList(actualTypes, actualElems));

        var returnCode = callResultType;
        var known = false;
        if (signature) {
            if (signature.returnResolved && signature.returns !== undefined) {
                returnCode = signature.returns;
                known = true;
            } else if (signature.expectedReturn !== undefined && signature.expectedReturn !== '?') {
                returnCode = signature.expectedReturn;
            } else if (signature.returns !== undefined) {
                returnCode = signature.returns;
                known = signature.returnResolved && signature.returns === '?';
            }
        }
        var returnType = (returnCode === 'S' && signature && signature.returnStruct
                ? signature.returnStruct                          /* by-value struct return -> the struct name */
                : (returnCode === 'p' && signature && signature.returnElem !== undefined
                        ? signatureCategoryForDesc(fullDescFor(returnCode, signature.returnElem))
                        : signatureReturnCategory(returnCode, known)));

        return appendOrigin('expects ' + label + '(' + paramsText + ') -> ' + returnType,
                            sourceName, sourceCode, sourceOffset);
    };

    updateCallExpectationComment = function (callInfo, callResultType) {
        if (!callInfo || callInfo.commentIndex === undefined || callInfo.commentIndex < 0) {
            return;
        }
        if (!metacode || callInfo.commentIndex >= metacode.length) {
            return;
        }

        var args = callInfo.commentArgs;
        if (!args) {
            return;
        }

        var refreshed = formatCallExpectationComment(
            args.name,
            args.signature,
            args.actualTypes,
            callResultType,
            args.sourceName,
            args.sourceCode,
            args.sourceOffset,
            args.actualElems
        );

        if (!refreshed) {
            return;
        }

        var entry = metacode[callInfo.commentIndex];
        if (entry && entry.operator === ';') {
            entry.operands[0] = refreshed;
        }
    };

    expectFunctionReturnType = function (name, expectedType, sourceCode, sourceOffset) {
        if (!isConcreteType(expectedType)) {
            return;
        }

        var signature = ensureFunctionSignature(name);
        if (!signature) {
            return;
        }

        if (signature.expectedReturn === undefined) {
            signature.expectedReturn = expectedType;
        } else if (signature.expectedReturn !== expectedType) {
            typeError(
                'Conflicting return type expectations for ' + name + ' ({$type1} vs {$type2})',
                sourceCode,
                sourceOffset,
                expectedType,
                signature.expectedReturn,
                'E304'
            );
        }

        if (signature.returnResolved && isConcreteType(signature.returns)
            && signature.returns !== expectedType) {

            typeError(
                'Return type mismatch for ' + name + ' ({$type1} vs {$type2})',
                sourceCode,
                sourceOffset,
                expectedType,
                signature.returns,
                'E304'
            );
        }
    };

    resolveFunctionReturnType = function (name, actualType, sourceCode, sourceOffset) {
        var signature = ensureFunctionSignature(name);
        if (!signature) {
            return;
        }

        signature.returnResolved = true;
        signature.returns = actualType;

        if (signature.expectedReturn !== undefined && isConcreteType(signature.expectedReturn)
            && isConcreteType(actualType) && signature.expectedReturn !== actualType) {

            typeError(
                'Return type for ' + name + ' does not match previous uses ({$type1} vs {$type2})',
                sourceCode,
                sourceOffset,
                actualType,
                signature.expectedReturn,
                'E304'
            );
        }
    };

    /* Called once per `function` DEFINITION, to emit the `name: FUNC` header. `kind === 'FUNC'` means a
       header was already emitted for this name, i.e. the name is being defined twice - which used to
       return silently, so the second body was emitted with NO header and the assembler reported the
       baffling "Parameters and locals must be declared at top of FUNC". Two units of an import closure
       defining the same function is the usual way in. */
    emitFunctionSignature = function (name, sourceCode, sourceOffset) {
        var entry = symbols.functions[name];
        if (!entry) {
            return;
        }
        if (entry.kind === 'FUNC') {
            fail('Function ' + name + ' is already defined',
                    sourceCode, sourceOffset, 'E401',
                    'each function needs a unique name across the whole import closure');
        }
        checkExternAgreement(name, entry.externProto, entry.signature, true, sourceCode, sourceOffset);

        var comment = formatFunctionSignatureComment(name, entry.signature);

        var entryType = (entry.type !== undefined ? entry.type : 'U');

        declare('FUNC',
                         'functions',
                         name,
                         entryType,
                         true,
                         undefined,
                         entry.sourceCode,
                         entry.sourceOffset,
                         comment);
    };

    /* One name may have at most one DEFINITION but any number of `extern` declarations, so long as
       every claim agrees: with the definition if the closure has one, and otherwise with each other.
       This does NOT police linkage - a prototype for a name nothing defines is a promise only
       gazl-validate can settle, and a name-only extern records no prototype and so asserts nothing.
       It fires exactly when the compiler is holding two claims itself, which an import closure makes
       routine since the builder compiles the whole closure as one unit. Names are not compared, only
       types, matching functionSignaturesCompatible() in tools/gazl-validate.nuxjs.js. */
    checkExternAgreement = function (name, claim, prior, priorIsDefinition, sourceCode, sourceOffset) {
        if (!claim || !prior) {
            return;
        }
        var got = signatureShape(name, claim), had = signatureShape(name, prior);
        if (got !== had) {
            failDisagreement(name, got, had, 'E437', priorIsDefinition, 'function', 'shapes',
                    sourceCode, sourceOffset);
        }
    };

    function signatureRoleForSection(section) {
        switch (section) {
            case 'CNST': return 'readonly';
            case 'TEMP': return 'temporary';
            default:     return 'global';
        }
    }

    /* Declare an `extern struct` interface for gazl-validate: the field names and types Impala
       compiled against, so the host-supplied layout (.o.Name.field / .z.Name) can be checked
       instead of silently trusted. Rendered as:
           ; signature extern struct Name { field : int, other : float-ptr } @ 12:1        */
    /* An array extent in a signature row is a CLAIM the validator compares against the other side. A
       literal or a single named const is a real claim; an extent that folded to a `<X>` compile-time
       scratch is not - the name is pool-recycled, so two unrelated extents both render `<A>` and
       compare EQUAL. Render those as the empty extent instead, the same wildcard a sizeless `extern
       array` already uses, which the validator skips rather than trusting. A row may state a fact or
       state "unknown"; it must never state something that merely looks like a fact. */
    function extentText(size) {
        var text = (size === undefined ? '' : '' + size);
        return '[' + (text.charAt(0) === '<' ? '' : text) + ']';
    }

    structSignatureRow = function (name, isExtern, sourceName, sourceCode, sourceOffset) {
        var s = structs[name];
        if (!s || !s.fields) {
            return undefined;
        }
        var parts = [];
        for (var i = 0; i < s.fields.length; ++i) {
            var f = s.fields[i];
            var cat;
            if (f.type === 'A') {                                 /* an array field advertises its extent + element */
                cat = signatureCategoryForDesc(f.elem) + extentText(f.size);
            } else if (f.type === 'S') {
                cat = f.struct;                                   /* a nested by-value struct field */
            } else {
                cat = (f.elem !== undefined ? signatureCategoryForDesc(fullDescFor(f.type, f.elem))
                                            : signatureParamCategory(f.type));
            }
            parts.push(f.name + ' : ' + cat);
        }
        return appendOrigin('signature ' + (isExtern ? 'extern ' : '') + 'struct ' + name
                                    + ' { ' + parts.join(', ') + ' }',
                            sourceName, sourceCode, sourceOffset);
    };

    formatGlobalSignatureComment = function (section, name, type, size, flavor,
                                                      sourceName, sourceCode, sourceOffset, elem) {
        if (!name) {
            return undefined;
        }

        var prefix = (exportNext ? 'export ' : '') + (flavor ? flavor + ' ' : '');

        if (type === 'A') {
            var extent = extentText(size);
            var elemCategory = signatureCategoryForDesc(elem);    /* typed arrays advertise their element chain; */
                                                                  /* untyped arrays keep the `unknown` wildcard */
            return appendOrigin('signature ' + prefix + 'array ' + name + extent + ' : ' + elemCategory,
                                sourceName, sourceCode, sourceOffset);
        }

        return appendOrigin('signature ' + prefix + signatureRoleForSection(section) + ' ' +
                            name + ' : ' + signatureCategoryForDesc(fullDescFor(type, elem)),
                            sourceName, sourceCode, sourceOffset);
    };

    function emitStandaloneSignatureComment(comment) {
        if (!comment) {
            return;
        }

        if (typeof output === 'function') {
            output('; ' + comment);
        }
    }

    /* `isExtern` marks a VALUELESS `const int N;` - external by omission of a value ("defined later by
       the GAZL run-time"). It spells no `extern` keyword, so it used to emit no row at all and was the
       one extern kind gazl-validate could not link-check (its compareExternSets("Const", ...) pass had
       nothing to compare). Emitting the row turns that pass on without touching the spelling. */
    formatConstSignatureComment = function (name, type, sourceName, sourceCode, sourceOffset, elem,
                                                     isExtern) {
        if (!name) {
            return undefined;
        }

        var prefix = (isExtern ? 'extern ' : (exportNext ? 'export ' : ''));
        return appendOrigin('signature ' + prefix + 'const ' + name + ' : '
                            + signatureCategoryForDesc(fullDescFor(type, elem)),
                            sourceName, sourceCode, sourceOffset);
    };

    /* 4  label & metacode helpers */
    newLabel = function (prefix) {
        var tag = (prefix === undefined ? '' : String(prefix));
        return '@.' + tag + (labelCounter++);
    };

    /* push a deep-cloned record into metacode */
    emitMeta = function (rec) {
        metacode.push(clone(metaSlot(rec)));
    };

    /* allocate new (empty) meta record, fill via makeMeta, then push */
    emit = function (op, type, op0, op1, op2) {
        var slot = {};                // fresh object
        makeMeta(slot, op, type, op0, op1, op2);  // user-supplied helper
        metacode.push(slot);
    };

    /* 5  portable replacement for ppeg.fail */
    fail = function (error, source, offset, code, hint) {
        function oneLine(s) { return replace(replace(replace(s,"\t",' '),"\r",' '),"\n",' '); }
        var message = bake(error);
        var hasSource = typeof source === 'string';
        var snippetSource = hasSource ? source : '';
        var snippetOffset = isFinite(offset) ? offset : 0;
        var before = oneLine(snippetSource.substr(snippetOffset - 8, 8));
        var after = oneLine(snippetSource.substr(snippetOffset, 40));
        var err = new Error(message + ' : ' + before + ' <!!!!> ' + after);
        err.impalaMessage = message;
        if (isFinite(offset)) {
            err.impalaOffset = offset;
        }
        err.impalaSnippetBefore = before;
        err.impalaSnippetAfter = after;
        if (code !== undefined) {
            err.impalaCode = code;                                /// stable diagnostic code, e.g. 'E201'
        }
        if (hint !== undefined) {
            err.impalaHint = hint;                                /// mechanical fix-it, rendered as a note line
        }
        throw err;
    };



    /* ---------------------------------------------------------
     *  Short-circuit / branch processing
     * --------------------------------------------------------- */
    processBranches = function () {
        var target      = { false: null, true: null }; // last FALSE / TRUE dest labels
        var targetCond  = null;                        // current branch condition (true / false)
        var currentGoto = null;                        // last unconditional goto
        var aliases     = {};                          // label alias map

        /* A `<-?` join may be DELETED only if this pass can still redirect every reference to it,
           and the backward walk can only do that for a reference it has NOT visited yet - one at a
           LOWER index. A reference from below (a `do`-while back-edge) had its operand fixed before
           the alias existed, and one from an operator the walk never rewrites (the `<> ==` guard an
           `assert` bakes its ok-label into) is never offered the alias at all. Either way the label
           has to stay, so work out which ones up front instead of sniffing the tag letter. */
        var pinned = {}, joinAt = {};
        for (var i = 0; i < metacode.length; ++i) {
            if (metacode[i].operator === '<-?') joinAt[metacode[i].operands[0]] = i;
        }
        for (var i = 0; i < metacode.length; ++i) {
            var rec = metacode[i];
            if (rec.operator === '<-?') continue;       // its own operand 0 is the definition, not a reference
            var rewritable = (rec.operator === '?->' || rec.operator === '-->');
            for (var k = 0; k < 3; ++k) {
                var at = joinAt[rec.operands[k]];
                if (at !== undefined && (!rewritable || i > at)) pinned[rec.operands[k]] = true;
            }
        }

        /* walk metacode bottom-to-top */
        for (var i = metacode.length - 1; i >= 0; --i) {
            var inst = metacode[i];

            switch (inst.operator) {

                /* ---  branch on TRUE / FALSE  --- */
                case '?->': {           /* created by AND/OR, e.g.   F->FALSE L1: */
                    targetCond         = inst.type; // boolean
                    var lbl            = inst.operands[0];
                    target[targetCond] = (lbl in aliases ? aliases[lbl] : lbl);
                    inst.operator      = null;      // remove
                    break;
                }

                case '<-?': {          /* unique FALSE/TRUE label, e.g.   FALSE L1: */
                    var lbl = inst.operands[0],
                        t   = inst.type;            // false / true
                    inst.type = null;               // will no longer be needed

                    if (target[t] != null) {        // label already chosen - make alias
                        aliases[lbl] = target[t];
                        inst.operator = (pinned[lbl] ? '<--' : null);
                    } else {
                        target[t]    = lbl;
                        inst.operator = '<--';      // we retain the label
                    }
                    break;
                }

                /* ---  invert ( NOT )  --- */
                case '!':
                    var tmp    = target.false;
                    target.false = target.true;
                    target.true  = tmp;
                    targetCond   = !targetCond;
                    inst.operator = null;
                    break;

                /* comment - ignore */
                case ';':
                    break;

                /* ---  unconditional GOTO  --- */
                case '-->': {
                    var lbl   = inst.operands[0];
                    var final = (lbl in aliases ? aliases[lbl] : lbl);
                    target.false = target.true = currentGoto = inst.operands[0] = final;
                    break;
                }

                /* record label after an optimised goto */
                case '<--':
                    if (currentGoto != null) {
                        aliases[inst.operands[0]] = currentGoto;
                    }
                    break;

                /* ---  comparison ops  --- */
                case '<=': case '<': case '>=': case '>': case '!=': case '==': {
                    /* if we are targeting FALSE, invert comparison */
                    if (targetCond === false) inst.operator = '!' + inst.operator;

                    /* move operands left and patch jump target */
                    inst.operands[0] = inst.operands[1];
                    inst.operands[1] = inst.operands[2];
                    inst.operands[2] = target[targetCond];

                    /* reset all branch state */
                    target.false = target.true = currentGoto = null;
                    break;
                }

                /* ---  anything else breaks the chain  --- */
                default:
                    target.false = target.true = currentGoto = null;
            }
        }

        /* POST-CONDITION. This pass OWNS every `@.` label - it mints them, aliases them and
           deletes them - so a reference left pointing at a name it also deleted is a bug HERE,
           and saying so beats the assembler's "Symbol not found" three layers downstream.
           A user label (`@name`) is not ours to promise, but it IS decidable: a label is always
           local to the function body, and by the time this runs the body is fully parsed, so the
           map below is the complete set. An undefined one is a user error, not an internal bug,
           so it gets a diagnostic at the `goto` rather than an assert. */
        var defined = {};
        for (i = 0; i < metacode.length; ++i) {
            if (metacode[i].operator !== '<--') continue;
            var name = metacode[i].operands[0];
            /* A user label (`@name`, never `@.` - those are ours and always unique) written twice
               mints two identical GAZL labels; the assembler then rejects "Symbol already defined"
               against a name and line the user never wrote. Decidable here - the map already exists. */
            if (defined[name] && name.charAt(1) !== '.' && metacode[i].labelOffset !== undefined) {
                fail('Duplicate label ' + name.substr(1),
                        metacode[i].labelSource, metacode[i].labelOffset, 'E446',
                        'a label may be defined once per function');
            }
            defined[name] = true;
        }

        /* COINCIDENT LABELS. A run of `<--` with nothing emitted between them all name the same
           address, but only one LINE can carry a name - so each of the others was spent on a `NOOP`
           whose entire job was to exist (adventCode had seven in a row). Collapse the run onto one
           survivor and rewrite the references to it. Safe HERE and not during the walk above: every
           alias and deletion is settled by now, so nothing re-points afterwards - which is what sank
           the prototype that tried to do this while walking. A user label survives in preference to a
           minted one, so a name someone can `goto` never disappears from the listing.
           NOT merged: a name holding `#` is a switch table entry (`.sN#k`), where the case value IS
           part of the name and two entries are different addresses that merely render alike here. */
        var alias = {}, run = [];
        for (i = 0; i <= metacode.length; ++i) {
            rec = (i < metacode.length ? metacode[i] : { operator: null });
            if (rec.operator === ';' || (rec.operator == null && i < metacode.length)) continue;
            if (rec.operator === '<--' && rec.operands[0].indexOf('#') < 0) {
                run.push(rec);
                continue;
            }
            for (var r = 1; r < run.length; ++r) {                /* a lone label has nothing to merge */
                var keep = run[0];
                for (var u = 0; u < run.length; ++u) {            /* prefer a name the user wrote */
                    if (run[u].operands[0].charAt(1) !== '.') { keep = run[u]; break; }
                }
                for (u = 0; u < run.length; ++u) {
                    if (run[u] === keep) continue;
                    alias[run[u].operands[0]] = keep.operands[0];
                    delete defined[run[u].operands[0]];           /* the reference check below now covers this */
                    keep.mayRide = (keep.mayRide === true && run[u].mayRide === true);
                    run[u].operator = null;
                }
                break;
            }
            run.length = 0;
        }
        if (alias !== undefined) {
            for (i = 0; i < metacode.length; ++i) {
                rec = metacode[i];
                if (rec.operator == null || rec.operator === ';') continue;
                for (var a = 0; a < 3; ++a) {
                    if (alias[rec.operands[a]] !== undefined) rec.operands[a] = alias[rec.operands[a]];
                }
            }
        }

        /* BRANCH THREADING and RETURN DUPLICATION (docs/GAZLAssemblerOptimizations.md items 4 and 5).
           The walk above resolves an alias AT VISIT TIME, so it is not transitive: a chain it meets in
           the wrong order survives. Here everything has settled, so following one is just a lookup.
              `GOTO @a` where `a:` leads to the function's RETU BECOMES that RETU - `RETU` takes no
           operands and does the same frame work wherever it stands, so this costs nothing but saves a
           dispatch on every execution. It is the common case, not a corner one: Impala has no `return`
           statement, so an early exit is a `goto` to the end label, which is where RETU sits.
              Branch THREADING (item 4) is not here, and the reason recorded until 2026-08-03 was WRONG.
           It was not "a label graph is unsafe to rewrite from inside the compiler". The cause, isolated
           by minimising `Priyome`: `! EQUi #DEBUG #0 @L` is NOT control flow. It tells the ASSEMBLER to
           stop emitting until it reaches `L`, so the target does not mean "continue here" - it delimits
           a REGION OF TEXT that will not exist. Thread it to a later label and the skipped region grows,
           swallowing the label DEFINITIONS inside it. Hence a symbol reported missing while plainly
           present in the listing, blamed on the NEXT function, where the scope closes with the forward
           reference still open. Two attempts failed to isolate that.
              Skipping records whose operator starts with `<> ` fixes it completely (verified: Priyome
           assembles, 0/87). It is still not here, for a duller reason - it then changes ONE line in the
           whole corpus, and that line is in a synthetic fixture. The backward walk above already
           collapses every chain that runs in the direction it scans, which is all of them in practice.
              Note this is also why the coincident-label merge above IS safe: it re-points a reference to
           a label at the SAME address, so no skip region moves. */
        var leadsTo = {};
        for (i = 0; i < metacode.length; ++i) {
            if (metacode[i].operator !== '<--') continue;
            for (var n = i + 1; n <= metacode.length; ++n) {
                var nx = (n < metacode.length ? metacode[n] : { operator: '--^' });
                if (nx.operator == null || nx.operator === ';' || nx.operator === '<--') continue;
                leadsTo[metacode[i].operands[0]] = nx;
                break;
            }
        }
        for (i = 0; i < metacode.length; ++i) {
            rec = metacode[i];
            if (rec.operator == null || rec.operator === ';' || rec.operator === '<--') continue;
            if (rec.operator === '-->' && leadsTo[rec.operands[0]] !== undefined
                    && leadsTo[rec.operands[0]].operator === '--^') {
                rec.operator = '--^';                             /* the GOTO IS the return */
                rec.operands[0] = undefined;
            }
        }

        for (i = 0; i < metacode.length; ++i) {
            var op = (rec = metacode[i]).operator;
            if (op == null || op === ';' || op === '<--') continue;
            for (var k = 0; k < 3; ++k) {
                var ref = rec.operands[k];
                if (typeof ref !== 'string' || ref.charAt(0) !== '@' || defined[ref]) continue;
                if (ref.charAt(1) === '.') {
                    assert(false, "branch to deleted label " + ref + " from " + op);
                } else if (rec.gotoOffset !== undefined) {
                    fail('goto to undefined label ' + ref.substr(1),
                            rec.gotoSource, rec.gotoOffset, 'E445',
                            'a label is local to its function - define it as `' + ref.substr(1)
                                    + ': ;` in this body, or remove the goto');
                }
            }
        }
    };

    /* ---------------------------------------------------------
     *  Pool / stock handling for transients    (‘%’, ‘<...>’)
     * --------------------------------------------------------- */

    /* assure no duplicates exist in a stock bucket */
    validateStock = function (cls) {
        var seen = {};
        var stk  = stock[cls];
        for (var i = 0; i < stk.length; ++i) {
            var tok = stk[i];
            assert(!seen[tok], "duplicate token in stock: " + tok);
            seen[tok] = true;
        }
        return true;
    };

    /* borrow one token from a stock bucket (or create a new one) */
    borrow = function (cls) {
        assert(validateStock(cls));

        var stk = stock[cls];
        if (stk.length) {
            return stk.pop();                      // reuse
        }

        /* otherwise mint a fresh id */
        if (cls === '%') {
            return '%' + (counters['%']++);
        }
        if (cls === '<') {
            /* THE POOL OWNS UPPERCASE `<A>`..`<Z>`, and that is the whole reason the hand-picked
               compile-time scratches are lowercase (`<a>` the struct-layout accumulator, `<t>` its
               element-size temp). Those two live across a whole layout block, outside the pool's
               borrow/return accounting, so a name the allocator can hand out would be clobbered by the
               next fold - and a struct layout routinely has a pooled scratch live at the same time
               (`! MULi <A> #23 #5` computing an extent while `<a>` accumulates the offset). The case
               is the only thing keeping the two namespaces apart; a new fixed scratch must be
               lowercase, and a new lowercase name must not already be taken. */
            var idx = counters['<']++;
            assert(idx < 26, "compile-time scratch pool exhausted (expression too complex)");
            return '<' + String.fromCharCode('A'.charCodeAt(0) + idx) + '>';
        }
        throw new Error("unknown stock class " + cls);
    };

    /* smart borrow for CALL args - first free id in last consecutive run */
    borrowForCall = function () {
        /* same safety check the original did */
        assert(validateStock('%'));

        var stk = stock['%'];

        /* empty ⇒ mint a brand-new one */
        if (stk.length === 0) {
            return counters['%']++;
        }

        /* A call window grows UPWARD from its base, so the base must sit above every live
           transient or the window would overlap one. The free pool is not always top-anchored:
           an out-of-order release (e.g. `a[i] = v` frees the index temp low while the value temp
           is kept high, or a destructure) can leave a freed hole below a live temp. When that
           happens - the highest allocated transient is live rather than free - mint a fresh slot
           above everything instead of reusing the hole. This is a latent allocator bug reachable
           in 1.0 too (`a[i] = v; b[i] = f(arg)` crashes the un-guarded allocator); the fix is
           byte-identical for the corpus, which never seats a call over a live temp. */
        var maxFree = -1;
        for (var _k = 0; _k < stk.length; ++_k) {
            var _v = parseInt(stk[_k].substr(1), 10);
            if (_v > maxFree) maxFree = _v;
        }
        if (maxFree < counters['%'] - 1) {
            return counters['%']++;
        }

        /* sort in-place on the numeric suffix, ascending           */
        stk.sort(function (a, b) {
            return parseInt(a.substr(1), 10) - parseInt(b.substr(1), 10);
        });

        /* walk backwards through the (now sorted) array,
           finding the *first* id in the last consecutive run       */
        var i = stk.length - 1,
            n = parseInt(stk[i].substr(1), 10);

        while (i >= 0 && stk[i] === '%' + n) {
            --i;
            --n;
        }
        ++i;               /* point at first element of the run  */
        ++n;               /* numeric id of the chosen transient */

        var chosen = n;
        stk.splice(i, 1);  /* remove from the pool                */

        /* Restore the stock order so ordinary borrow() calls reuse
           the most recently freed registers first, matching the
           original queue semantics. */
        stk.reverse();

        /* duplicate-check, like the original assert(validate...)    */
        assert(validateStock('%'));
        return chosen;
    };

    /* reserve transient %number (used to hold multi-return output slots) */
    claimSlot = function (number) {
        if (counters['%'] === number) {
            ++counters['%'];
        } else {
            assert(counters['%'] > number);
            var stk = stock['%'];
            for (var idx = stk.length - 1; idx >= 0 && stk[idx] !== '%' + number; --idx) {}
            assert(idx >= 0, "transient %" + number + " must exist in stock");
            stk.splice(idx, 1);
        }
    };

    /* put a token back into its stock bucket */
    function stockContains(stk, op) {
        for (var i = stk.length - 1; i >= 0; --i) {
            if (stk[i] === op) {
                return true;
            }
        }
        return false;
    }

    returnBack = function (op) {
        if (op == null) {
            return;
        }
        var c = op[0];

        /* A compile-time scratch carried as the trailing `<X>` token of a COMPOUND operand:
           `*<A>` (alloc size), `#<D>` (pointer offset), `&bank:<D>` / `$v:<D>` (base:offset), and
           `%<A>` (a symbolically indexed transient). This must be tested BEFORE the bare-token case:
           `%<A>` starts with '%' but is not a slot number, and pushing it into the transient stock
           both loses the scratch and poisons the pool with a token no allocator can hand out. */
        if (op.length > 3 && op.charAt(op.length - 1) === '>' && op.charAt(op.length - 3) === '<') {
            returnBack(op.substr(op.length - 3));
        }
        /* a bare transient / compile-time scratch */
        else if (c === '%' || c === '<') {
            var stk = stock[c];
            if (!stockContains(stk, op)) stk.push(op);   // avoid dupes
        }
    };

    /* Align with the original PPEG helper while avoiding the reserved
       `return` identifier in generated JavaScript. */
    $$parser["return"] = returnBack;

    /* --------------------------------------------------------- *
     *  Debug helpers & meta-record construction / destruction   *
     * --------------------------------------------------------- */

    /* pretty-print one meta-instruction (only when it has op) */
    debugPrintMeta = function (m) {
        m = metaSlot(m);
        if (m && m.operator != null) {
            console.log(
                '{' + m.operator + '}(' + m.type + ') {'
                     + m.operands[0] + '} {' + m.operands[1]
                     + '} {' + m.operands[2] + '}'
            );
        }
    };

    /* lazily materialise a meta-record for any parse node */
    function metaSlot(node) {
        if (node == null || (typeof node !== 'object' && typeof node !== 'function')) {
            return { operator: undefined, type: undefined,
                     operands: [ undefined, undefined, undefined ] };
        }
        if (node.operands !== undefined) {
            if (!Array.isArray(node.operands)) {
                node.operands = [ undefined, undefined, undefined ];
            } else {
                while (node.operands.length < 3) {
                    node.operands.push(undefined);
                }
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'operator')) {
                node.operator = undefined;
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'type')) {
                node.type = undefined;
            }
            return node;
        }

        if (!Object.prototype.hasOwnProperty.call(node, '_')) {
            if (node.operands === undefined) {
                node.operands = [ undefined, undefined, undefined ];
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'operator')) {
                node.operator = undefined;
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'type')) {
                node.type = undefined;
            }
            return node;
        }

        var slot = node._;
        if (!slot || slot.operands === undefined) {
            slot = { operator: undefined, type: undefined,
                     operands: [ undefined, undefined, undefined ] };
            node._ = slot;
        }
        return slot;
    }

    createParserContext = function () {
        return {
            _: { operator: undefined, type: undefined,
                 operands: [ undefined, undefined, undefined ] }
        };
    };

    /* overwrite the fields of an existing meta object */
    function normaliseVoid(value) {
        return value === null ? undefined : value;
    }

    makeMeta = function (rec, op, type, op0, op1, op2) {
        rec = metaSlot(rec);
        rec.operator  = normaliseVoid(op);
        rec.type      = normaliseVoid(type);
        rec.operands  = [
            normaliseVoid(op0),
            normaliseVoid(op1),
            normaliseVoid(op2)
        ];
        /* A value meta is never a place. Meta slots are pooled and reused, so clear any leftover
           place/window state from a previous use (e.g. `*p = v` leaves a struct place on the slot
           that `p` is later looked up into) - otherwise fieldAccess mis-reads `p` as a struct value. */
        rec.place     = false;
        rec.baseKind  = undefined;
        rec.base      = undefined;
        rec.offParts  = undefined;
        rec.root      = undefined;
        rec.arrayOf   = undefined;
        rec.extent    = undefined;
        rec.oobIndex  = undefined;
        rec.struct    = undefined;
        rec.dynIndex  = undefined;
        rec.winBase   = undefined;
        rec.winWords  = undefined;
        rec.readonly  = false;        /* pooled slots: never inherit a previous symbol's writability */
        return rec;
    };

    /* release all three operands contained in a meta-record */
    releaseMeta = function (meta) {
        meta = metaSlot(meta);
        for (var i = 2; i >= 0; --i) {
            returnBack(meta.operands[i]);
        }
    };

    /* --------------------------------------------------------- *
     *  R-value helpers                                          *
     * --------------------------------------------------------- */

    /**
     * Convert an expression into an r-value, allocating a transient
     * when needed.  `classes` defaults to '#<&^$%'.
     */
    /* A CONSTANT index against a KNOWN extent. One rule for every array, called from both subscript
       paths - a struct field keeps its extent on a place, a plain array decayed to a pointer and carries
       it on the meta, and there is no reason for `g[9]` and `s.v[9]` to report differently. Undecidable
       when either side is symbolic (`v[SN]`, a runtime index); those fall through to --range-checks. The
       A NEGATIVE index is caught here too: `v[-1]` folds to a legal-looking positive offset inside the
       struct and writes BACKWARDS into whatever field precedes the array.

       PAST THE END IS NOT REPORTED HERE, because whether it is an error depends on what happens next,
       and the rule is `docs/Impala2Review.md`'s: a DEREFERENCE with an out-of-range constant index is a
       guaranteed trap, so it is a compile error, while ADDRESS FORMATION (`&a[7]`, `&p[[i]]`) is always
       legal and never checked - an out-of-range address is a value like any other, and GAZL's own check
       fires on access operands, not on address-taking. So the finding is returned as a flag; reference()
       clears it and checkIndexUse reports on every use that actually reads or writes the element. That
       is also why Impala needs no one-past-the-end carve-out where C does; verified to `&g[1000000]`.

       BEFORE THE START is different, and fails outright - address or not. It is not "an address like any
       other": GAZL cannot even represent it on the direct form (`MOVp $p &g:-1` and `ADRL $p $a:-1` are
       both rejected at assembly, "Negative value not accepted"), and on the FOLDED form it is worse than
       rejected - `.o.S.pad + (-1)` is a perfectly good positive offset naming the field before it, so
       `&s.pad[-1]` assembles, runs, and aliases a neighbour. One source-level mistake had three different
       outcomes across the shapes; this is the one place that can give it one. */
    /* WHAT can decide this index, and WHEN. Every tier boundary reads this ONE table, because the three
       spellings of "constant" - `#123`, `#KONST` and a folded `<A>` - used to be sorted by three separate
       regexes in two functions whose union was never the whole operand universe, and each gap that left
       was found by hand after shipping rather than by a test. A new operand form now lands in a case that
       has to be named.

       A FOLDED scratch gets its OWN kind rather than being lumped in with either neighbour, because the
       two questions callers ask part company here. It is an assemble-time value, so it folds into an
       offset like any other constant. But `<A>` is recycled - the next folded subscript in the same
       function rebinds it (`! ADDi <A> #K #1` ... `! ADDi <A> #M #1`) - so it can never KEY a bounds
       assertion, which dedups by index text and would then answer for whichever value landed last. */
    indexKind = function (op) {
        if (/^#?<[A-Za-z]>$/.test(op)) return 'scratch';          /* an assemble-time fold under a recycled name */
        if (op.charAt(0) !== '#') return 'runtime';               /* $local, %transient */
        if (/^#-?[0-9]+$/.test(op)) return 'now';                 /* a literal - Impala decides it */
        return /^#[A-Za-z_]/.test(op) ? 'assembly' : 'runtime';   /* #NAME is the assembler's */
    };

    checkConstIndex = function (extent, idxRV, sourceCode, sourceOffset) {
        var kind = indexKind(idxRV);
        if (extent === undefined || kind === 'runtime' || kind === 'scratch') {
            return undefined;                                     /* tier 3's, if it is enabled at all */
        }
        if (kind === 'assembly') {
            /* A SYMBOLIC constant index - `a[KONST]`: the ASSEMBLER can evaluate the operand and Impala
               cannot. Decidable, just not now, so defer it exactly as a symbolic EXTENT is deferred
               below. Same scoping too: a plain array is caught natively (`&g:KONST` -> "Offset out of
               bounds: g"), a struct field is not. BOTH ends have to be asked, because neither is
               knowable here. */
            return extent.inField
                    ? { k: idxRV.substr(1), ext: extent, src: sourceCode, off: sourceOffset }
                    : undefined;
        }
        var ck = constInt(idxRV);
        if (ck < 0) {                                             /* decidable WITHOUT the extent */
            fail('Index ' + ck + ' is before the start of this array',
                    sourceCode, sourceOffset, 'E461',
                    'a negative index is not a legal address either');
        }
        var cn = constInt('#' + extent.n);
        if (cn === undefined) {
            /* TIER 2 - the extent is a symbol only the assembler can resolve. Deferred for a struct array
               FIELD, and ONLY there, because that is the only place nothing else looks: the overrun stays
               inside the struct's allocation, so `Symbols::resolve` sees a legal offset. A plain array
               with a symbolic extent is already caught natively (`a[7]` on `a[SN]` -> "Offset out of
               bounds: a"), and re-checking it here would put three assemble-time lines into the shipped
               text of the canonical `const int N; array a[N]` idiom to say what the assembler says for
               free - 15 of 87 goldens grew when this was not scoped. `stride` carries a struct-element
               field's element size, because `.z.` counts WORDS and the index counts elements. */
            if (!extent.inField) {
                return undefined;
            }
        } else if (ck < cn) {
            return undefined;                                     /* in range, and decided right here */
        }
        return { k: ck, n: cn, ext: extent, src: sourceCode, off: sourceOffset };
    };

    /* `--range-checks`: the DEBUG-gated RUNTIME bounds test for a subscript whose index is not a
       compile-time number, which is the only tier the two static ones cannot reach.

       TWO COMPARES AND NOTHING ELSE. The index is the only value not known until run time: the bound is
       the `.z.` SYMBOL, so it is an assemble-time immediate and needs no instruction to produce - which
       also makes this work unchanged for an extent the assembler resolves (`v[SN]`). For a struct element
       the operand handed in is the SCALED word offset, which is what `.z.` counts, and that multiply was
       already emitted for the access itself.

       An earlier version was branchless - `(extent - 1 - i) | i` has its sign bit set exactly when `i` is
       out of range, which is one conditional branch instead of two. That is the wrong trade: it spends
       three ALU instructions to save one branch - six runtime instructions against four, and FOUR against
       TWO on the path that matters, since an in-range access never reaches the failure call - and it
       computes `extent - 1` at RUN time from a constant the assembler already knows. GAZL fuses the
       compare and the branch, so each bound really is one instruction. Two compares also read as what
       they are.

       OFF BY DEFAULT, and that is not the same switch as `#DEBUG`. `DEBUG` decides whether the
       assembler EMITS the instructions; this decides whether they are in the .gazl TEXT at all - and
       the text is the shipped artifact, embedded into C++ via tools/gazlCompactor. Measured, an assert
       costs ~95 bytes of compacted source that `DEBUG 0` does not remove. */
    emitRangeCheck = function (idxOp, extent, sourceCode, sourceOffset) {
        if (!rangeChecks || extent === undefined
                || indexKind(idxOp) !== 'runtime') {
            return;                                               /* a constant, in any of its spellings,
                                                                     belongs to an earlier tier */
        }
        var ok  = beginDebugGuard('r');
        var bad = newLabel('r');
        emit('<', 'i', undefined, idxOp, '#0');           /* below the start -> fail */
        emit('?->', true, bad, undefined, undefined);
        emit('<', 'i', undefined, idxOp, '#' + extent.sym);   /* inside the extent -> ok */
        endDebugGuard(ok, {}, 'index out of range: ' + extent.what,
                sourceCode, sourceOffset, bad);
    };

    /* The two halves of a DEBUG-gated runtime check, shared by `assert` and by --range-checks so the
       protocol has one home: an assemble-time skip when `DEBUG` is 0, then (caller emits its comparison)
       a conditional jump past the failure call, the `^assertFail` call with its message constant, and the
       join. processBranches understands only this shape - the comparison must sit immediately before the
       `?->` - so a change to the assert-string protocol, assertFail's arity or the branch pair belongs
       here rather than in two places that must be kept bit-identical. `msg` is the meta slot the message
       constant is built into; `assert` passes its own so the string is its parsed expression text. */
    beginDebugGuard = function (tag) {
        var ok = newLabel(tag);
        emit('<> ==', 'i', '#DEBUG', '#0', ok);
        return ok;
    };

    endDebugGuard = function (ok, msg, text, sourceCode, sourceOffset, failLabel) {
        emit('?->', true, ok, undefined, undefined);
        if (failLabel !== undefined) {                            /* a second test that jumps straight to
                                                                     the failure lands here */
            emit('<-?', true, failLabel, undefined, undefined);
        }
        var r = borrowForCall();
        makeString('a', msg, text, sourceCode, sourceOffset);
        makeArgValue(msg, r + 1);
        emit('()', '?', '^assertFail', '%' + r, '*1');
        returnBack('%' + (r + 1));
        returnBack('%' + r);
        emit('<-?', true, ok, undefined, undefined);
    };

    /* Both tiers of a subscript's bounds checking, in the order they must run: the constant one decides
       without emitting, the runtime one emits only when the constant one declined. Callers get the
       out-of-range finding back to hang on the meta they build. */
    checkSubscript = function (extent, idxOp, sourceCode, sourceOffset) {
        emitRangeCheck(idxOp, extent, sourceCode, sourceOffset);
        return checkConstIndex(extent, idxOp, sourceCode, sourceOffset);
    };

    /* A constant index that tier 1 could not clear survived the subscript as a flag, because only its USE
       decides whether it is an error. Anything that reads or writes the element lands here; `&` cleared
       it. A numeric extent is decided now; a symbolic one becomes a DEFERRED assertion, asked of the
       assembler in the canonical `! LSSi` / `! FAIL` / skip-label form (docs/TwoStageConstants.md rule 4,
       the same shape assertFitsExtent uses for an over-filled initializer). It costs no runtime
       instruction and is emitted only at a real dereference, so `&s.v[9]` stays legal here too. */
    checkIndexUse = function (expr) {
        var op = expr.oobIndex;
        if (op === undefined) return;
        expr.oobIndex = undefined;
        if (op.n !== undefined) {
            fail('Index ' + op.k + ' is outside the extent ' + op.n + ' of this array',
                    op.src, op.off, 'E461',
                    'valid indices are 0 to ' + (op.n - 1)
                            + ' - taking its ADDRESS is legal, reading or writing it is not');
        }
        /* Emitted RIGHT HERE, immediately before the access it guards. It used to be queued and flushed at
           the end of the function, because `declare` writes straight to the output stream and flushes
           pending metacode first, which a mid-expression call cannot survive. `emit` has neither problem:
           it appends to metacode in order, exactly as the subscript's own `! ADDi` does. The only thing
           deferral ever bought was collapsing several indices into one array down to the largest, and
           that never fired once in the corpus - every duplicate was the SAME index accessed twice (read
           and written), which a per-function seen-set removes just as well.

           Emitting in place also costs one line less: the skip label rides on the guarded instruction
           instead of needing a bare `!` of its own. */
        /* An OWNED copy is never deduplicated and never scaled: it was taken from the folded value the
           subscript pushed into the offset, which is already in `.z.` units, and its name is a recycled
           scratch that says nothing about which index it holds - so it cannot key anything either. */
        var key = op.own ? undefined : op.ext.sym + '|' + op.k;
        if (key !== undefined) {
            if (emittedGuards[key]) {
                return;                                           /* the same assertion, already asked */
            }
            emittedGuards[key] = true;
        }
        var ok  = newLabel('g');
        var lhs = '#' + op.k;
        var w;
        if (op.ext.stride !== undefined && !op.own) {             /* `.z.` counts WORDS, the index counts
                                                                     ELEMENTS - scale before comparing */
            w = borrow('<');
            emit('<> *', 'i', w, lhs, '#' + op.ext.stride);
            lhs = '#' + w;
        }
        var low;
        if (typeof op.k !== 'number') {
            /* Anything but a literal may be negative, so it needs BOTH bounds (a literal was settled by
               E461). Both are plain comparisons: the low one falls through into the FAIL, whose label
               RIDES it - assemble-time branches resolve against a line that folds away. This used to be
               `(extent - 1 - k) | k >= 0`, three extra ALU ops bought purely to avoid a second label
               back when flushMetaCode spent every one on a NOOP. */
            low = newLabel('g');
            emit('<> <', 'i', lhs, '#0', low);
        }
        emit('<> <', 'i', lhs, '#' + op.ext.sym, ok);
        if (w !== undefined) { returnBack(w); }
        if (low !== undefined) {
            emit('<-?', true, low, undefined, undefined);
            metacode[metacode.length - 1].mayRide = true;   /* only the `! FAIL` below */
        }
        emit('<> FAIL', undefined,
                (op.own ? 'a computed index' : 'index ' + op.k)
                        + ' outside ' + op.ext.what + ' (resolved only at assembly)',
                undefined, undefined);
        emit('<-?', true, ok, undefined, undefined);
        metacode[metacode.length - 1].mayRide = true;   /* the guarded instruction, or the
                                                                             next guard's first comparison */
        if (op.own) {
            returnBack(op.k);                            /* held since the subscript, for exactly this */
        }
    };

    /* ONE deferred assertion, in the canonical `! <CMP>` / `! FAIL <text>` / skip-label form
       (docs/TwoStageConstants.md rule 4): the assembler decides what Impala could not, aborts with a real
       sentence, and the whole thing costs no runtime instruction. Every caller goes through here so the
       shape has one home - it had two before, drifting apart by a `declare` argument at a time. `.g<N>`
       uses guardCounter, NOT labelCounter, which resets per function. */
    assembleAssert = function (tests, message, sourceCode, sourceOffset) {
        var ok = '.g' + (guardCounter++);
        var bad = (tests.length > 1) ? '.g' + (guardCounter++) : ok;
        for (var i = 0; i < tests.length; ++i) {
            /* The LAST test jumps past the failure when it holds; every earlier one jumps TO it, so a
               caller with two bounds supplies the first already inverted. Two tests share one `! FAIL`
               and one message - the same two-tests-one-failure shape the runtime tier uses. */
            declare('! ' + tests[i][0], 'globals', undefined, 'i', true,
                    tests[i][1] + ' @' + (i + 1 < tests.length ? bad : ok), sourceCode, sourceOffset);
        }
        declare('! FAIL ' + message, 'globals', (bad === ok ? undefined : bad),
                undefined, true, undefined, sourceCode, sourceOffset);
        declare('!', 'globals', ok, undefined, true, undefined, sourceCode, sourceOffset);
    };

    makeRValue = function (expr, classes) {
        classes = classes || '#<&^$%';

        expr = metaSlot(expr);
        checkIndexUse(expr);                              /* reading it - reference() would have cleared the flag */

        if (expr.place && expr.arrayOf) {                         /* array place used without a subscript -> decay to a pointer */
            var delem = expr.arrayOf;
            var dt = placeAddress(expr);
            makeMeta(expr, ':=', 'p', undefined, dt, undefined);
            setElem(expr, delem);
            return dt;
        }

        var op   = expr.operator;
        var op1  = expr.operands[1];

        var op2  = expr.operands[2];
        var op1Prefix = (op1 ? op1[0] : '');
        var op2Prefix = (op2 ? op2[0] : '');

        /* already a simple l-value we can reuse? */
        if ((op === '=' || op === ':=') &&
            op1Prefix && span(op1Prefix, classes) === 1) {
            return op1;
        }

        /* otherwise evaluate into a transient */
        returnBack(op2);
        returnBack(op1);

        var cls = '%';              /* default stock   */
        var t   = op1Prefix + op2Prefix;

        if (t.length > 0 && span(t, '#<') === t.length && span('<', classes) === 1) {
            expr.operator = '<> ' + op;     /* compile-time op */
            cls = '<';
        }

        var tmp = borrow(cls);
        expr.operands[0] = tmp;

        emitMeta(expr);
        return tmp;
    };

    /**
     * Ensure an expression’s value ends up in the given
     * transient “%<number>”.
     */
    makeArgValue = function (expr, number) {
        expr = metaSlot(expr);

        var op   = expr.operator;
        var tgt  = '%' + number;
        var op1  = expr.operands[1];
        var op2  = expr.operands[2];

        /* already fine? */
        if ((op === '=' || op === ':=') && op1 === tgt) {
            return;
        }

        returnBack(op2);
        returnBack(op1);

        claimSlot(number);           /* remove %<number> from the free list if present */

        expr.operands[0] = tgt;
        emitMeta(expr);
    };

    /* --------------------------------------------------------- *
     *  Typed error helper                                       *
     * --------------------------------------------------------- */

    function verboseType(t) {                                  /// never leave a type blank in a diagnostic
        var v = VERBOSE_TYPES[t];
        return (v !== undefined ? v : (t !== undefined ? "'" + t + "'" : 'unknown'));
    }

    typeError = function (desc, source, offset, type1, type2, code, hint) {
        var message = replace(desc, '{$type1}', verboseType(type1));
        if (type2 !== undefined) {
            message = replace(message, '{$type2}', verboseType(type2));
        }
        fail(message, source, offset, code, hint);
    };

    /* --------------------------------------------------------- *
     *  Strict-expression helpers (Impala 2)                     *
     * --------------------------------------------------------- */

    /* `return`, `break` and `continue` are reserved, and none of them is in the KEYWORD alternation:
       `return` is a statement rule of its own, and the other two have no lowering at all. So nothing
       stopped any of them being used as a NAME, and the complaint then landed at the USE naming the
       wrong thing - `locals int return` was accepted, and `return = 5;` reported E448 "return does not
       take a value", while `break`/`continue` reached E403 "Undeclared identifier". One rule, called
       from every door that takes a name. Still a --legacy warning: in 1.x these WERE ordinary
       identifiers, so old code that used one has to keep building. */
    RESERVED_NAMES = { 'return': true, 'break': true, 'continue': true };
    checkReservedName = function (name, what, sourceCode, sourceOffset) {
        if (RESERVED_NAMES[name] !== true) {
            return;
        }
        strictError("'" + name + "' is a reserved word, not a"
                        + ('aeiou'.indexOf(what.charAt(0)) < 0 ? ' ' : 'n ') + what + " name",
                sourceCode, sourceOffset, 'E449',
                'rename the ' + what + '; --legacy still accepts a reserved word here');
    };

    strictError = function (message, source, offset, code, hint) {   /// error by default; warning under `--legacy`
        if (!legacyMode) {
            fail(message, source, offset, code, hint);
        } else if (typeof _hostOptions !== 'undefined' && _hostOptions != null
                && typeof _hostOptions.warn === 'function') {
            _hostOptions.warn(message, offset, code, hint);
        }
    };

    /* The integer behind a folded operand, or undefined when it is symbolic (`<A>`, a host `! DEFi`, an
       `extern struct` size). EVERY compile-time check gates on this: Impala may only reject a value it
       genuinely knows, and not knowing is not the same as being fine - so a symbolic operand is passed
       over in silence rather than guessed at. See docs/CompileTimeHardening.md. */
    constInt = function (operand) {
        return (typeof operand === 'string' && /^#-?[0-9]+$/.test(operand))
                ? parseInt(operand.substr(1), 10) : undefined;
    };

    /* `SWCH` resolves table entries 0..size-1 only, so a case outside that window is unreachable - and a
       NEGATIVE offset folds to `.sN.-6`, which the assembler rejects as an invalid identifier, failing a
       build the compiler accepted without a word. A repeated value mints the same `.sN#K` label twice and
       trips `Symbol already defined` on a name the user never wrote. They need DIFFERENT things: the
       window check wants a numeric range, the duplicate check wants only the values, so a symbolic
       range narrows this to the duplicate half rather than switching both off. */
    checkCaseValue = function (ctx, value, source, offset) {
        if (ctx === undefined || value === undefined) {
            return;
        }
        /* Key the duplicate check on the RAW value. It used to read `caseSeen[value - fromNum]`, which
           borrowed the range base it has no need of and put it behind the same early return as the
           window check - so a SYMBOLIC range (`switch (i == LO to HI)`, `LO` a named const, which
           `constInt` deliberately never folds) silently disabled it. The two arms then minted `.sN#K`
           twice and the build died at assembly on `Symbol already defined: .s0.0`, naming a
           compiler-minted label the user never wrote. A repeat is a repeat whatever the base is. */
        if (ctx.caseSeen[value] === true) {
            fail('Duplicate case value ' + value, source, offset, 'E443',
                    'each case value may appear once in a switch');
        }
        ctx.caseSeen[value] = true;
        if (ctx.fromNum === undefined) {
            return;              /* symbolic range: the window is genuinely unknowable here - see S5 */
        }
        /* Subtract here rather than reading the emitted offset: `subConstInt` defers to an assemble-time
           `! SUBi <A>` whenever `from` is non-zero, so the offset operand is symbolic in exactly the
           `switch (i == 5 to 9)` shape that produces the unloadable `.sN.-6`. */
        var off = value - ctx.fromNum;
        if (ctx.sizeNum !== undefined && (off < 0 || off >= ctx.sizeNum)) {
            fail('Case value ' + value + ' is outside the switch range '
                    + ctx.fromNum + ' to ' + (ctx.fromNum + ctx.sizeNum), source, offset, 'E444',
                    'the upper bound is exclusive, so the last reachable case is '
                            + (ctx.fromNum + ctx.sizeNum - 1));
        }
    };

    mixedBitwise = function (first, op, source, offset) {
        strictError("Mixed bitwise operators ('" + first + "' and '" + op
                + "') require parentheses", source, offset, 'E101',
                'add parentheses to keep the current meaning');
    };

    stampBitwise = function (rec, flag) {                /// mark whether `rec` folded top-level bitwise ops
        metaSlot(rec).bitwiseTop = flag;
    };

    checkCompMix = function (leftRec, rightRec, source, offset) {
        if (metaSlot(leftRec).bitwiseTop === true || metaSlot(rightRec).bitwiseTop === true) {
            strictError('Comparison mixed with bitwise operators requires parentheses',
                    source, offset, 'E102',
                    'add parentheses around the bitwise expression to keep the current meaning');
        }
    };

    /* --------------------------------------------------------- *
     *  Element-type descriptors (Impala 2 typed pointers/arrays) *
     *  A descriptor is a ':'-separated chain of atoms: 'i', 'f', *
     *  'p:i', 'p:p:i'. A terminal atom may be a struct NAME      *
     *  ('Filter', 'p:Filter') for typed struct pointers/arrays.  *
     * --------------------------------------------------------- */

    descHead = function (desc) {                         /// 'p:i' -> 'p'; 'Filter' -> 'Filter'
        if (desc === undefined) return undefined;
        var colon = desc.indexOf(':');
        return (colon === -1 ? desc : desc.substr(0, colon));
    };

    descTail = function (desc) {                         /// 'p:i' -> 'i'; 'i' -> undefined
        if (desc === undefined) return undefined;
        var colon = desc.indexOf(':');
        return (colon === -1 ? undefined : desc.substr(colon + 1));
    };

    isStructAtom = function (atom) {                     /// is `atom` a defined struct name?
        return atom !== undefined && structs
                && Object.prototype.hasOwnProperty.call(structs, atom);
    };

    isFuncTypeAtom = function (atom) {                   /// is `atom` a named funcptr type (Step 3)?
        return atom !== undefined && functypes
                && Object.prototype.hasOwnProperty.call(functypes, atom);
    };

    /* --------------------------------------------------------- *
     *  Named function-pointer types  (Impala 2 Step 3)          *
     * --------------------------------------------------------- */

    /* A functype emits NOTHING - no symbol, no layout, not even a `; signature` row - so unlike a
       struct definition (which owns its `.o.`/`.z.` constants) or a function (which owns a FUNC
       label) there is no artifact for a second declaration to collide with. Re-declaring one is
       therefore free PROVIDED the two agree, which is what lets a unit declare the functypes it uses
       and still be imported alongside another unit that declares the same ones. Set the earlier one
       aside and let endFuncType compare; emitting nothing also means gazl-validate never sees a
       functype, so this is the only place the disagreement can be caught at all. */
    beginFuncType = function (name, sourceCode, sourceOffset) {
        claimTopName(name, 'functype', sourceCode, sourceOffset);
        var shadowed = (isFuncTypeAtom(name) ? functypes[name] : undefined);
        functypes[name] = { shadowed: shadowed, params: [], returnList: [], returnCount: 0,
                returns: 'V', returnElem: undefined, returnStruct: undefined, returnWords: 0,
                complete: false, sourceCode: sourceCode, sourceOffset: sourceOffset,
                sourceName: sourceName };
    };

    /* By-value struct params/returns are PARKED for Impala 3.0 (docs/ParkedFeatures.md). Every door that
       can introduce one rejects it HERE, so there is one owner instead of a copy per door - a `functype`
       declarator was previously unguarded, which let the whole parked by-value path run and, for an
       `extern struct`, baked a numeric COPY size against a host-owned layout. */
    rejectByValueStruct = function (type, struct, pname, isReturn, sourceCode, sourceOffset) {
        if (type !== 'S') return;
        if (isReturn) {
            fail('Returning a struct by value is not supported in Impala 2.0',
                    sourceCode, sourceOffset, 'E427',
                    'return it through a ' + struct + ' pointer out-parameter');
        }
        fail('Passing a struct by value is not supported in Impala 2.0',
                sourceCode, sourceOffset, 'E426',
                'take it by pointer: ' + struct + ' pointer '
                        + (pname !== undefined ? pname : struct.charAt(0).toLowerCase()));
    };

    addFuncTypeParam = function (name, type, elem, struct, words, pname, sourceCode, sourceOffset) {
        rejectByValueStruct(type, struct, pname, false, sourceCode, sourceOffset);
        functypes[name].params.push({ type: type, elem: elem,
                struct: struct, words: words, name: pname });
    };

    addFuncTypeReturn = function (name, type, elem, struct, words, sourceCode, sourceOffset) {
        rejectByValueStruct(type, struct, name, true, sourceCode, sourceOffset);
        functypes[name].returnList.push({ type: type, elem: elem, struct: struct, words: words });
    };

    endFuncType = function (name) {
        var ft = functypes[name];
        ft.complete = true;
        ft.returnCount = ft.returnList.length;
        if (ft.returnCount > 0) {
            ft.returns      = ft.returnList[0].type;
            ft.returnElem   = ft.returnList[0].elem;
            ft.returnStruct = ft.returnList[0].struct;
            var rw = 0;
            for (var i = 0; i < ft.returnList.length; ++i) {
                rw += (ft.returnList[i].type === 'S' ? ft.returnList[i].words : 1);
            }
            ft.returnWords = rw;
        } else {
            ft.returns = 'V';
            ft.returnWords = 0;
        }
        var held = ft.shadowed;
        if (held) {
            var got = signatureShape(name, ft), had = signatureShape(name, held);
            if (got !== had) {                                    /* never a definition: a functype has only declarations */
                fail('functype ' + name + ' is already declared with a different shape: "'
                        + got + '" vs "' + had + '"', ft.sourceCode, ft.sourceOffset, 'E440',
                        'one functype cannot have two shapes - make the declarations identical, or keep only one');
            }
        }
    };

    /* Does a concrete function signature satisfy a named funcptr type? Compares arity, each
       parameter's type (+ struct / pointer-element), and the return shape. */
    funcTypeMatches = function (ftName, signature) {
        var ft = functypes[ftName];
        if (!ft || !signature) return true;                       /* unknown -> don't over-report */
        var fp = ft.params || [], sp = signature.params || [];
        if (fp.length !== sp.length) return false;
        for (var i = 0; i < fp.length; ++i) {
            if (fp[i].type !== sp[i].type) return false;
            if (fp[i].type === 'S' && fp[i].struct !== sp[i].struct) return false;
            if (fp[i].type === 'p' && fp[i].elem !== undefined && sp[i].elem !== undefined
                    && fp[i].elem !== sp[i].elem) return false;
        }
        var sc = (signature.returnCount === undefined ? (signature.returns === '?' ? 0 : 1)
                                                      : signature.returnCount);
        if ((ft.returnCount || 0) !== sc) return false;
        if (ft.returns !== signature.returns) return false;
        if (ft.returns === 'S' && ft.returnStruct !== signature.returnStruct) return false;
        return true;
    };

    elemVerbose = function (desc) {                      /// 'p:i' -> "int pointer"; 'p:Filter' -> "Filter pointer"
        if (desc === undefined) return 'untyped';
        var head = descHead(desc);
        var tail = descTail(desc);
        if (head === 'F') {                                       /* funcptr: named type or bare "funcptr" */
            return (tail !== undefined ? tail : 'funcptr');
        }
        if (tail === undefined) {
            return ((isStructAtom(head) || isFuncTypeAtom(head))
                    ? head : VERBOSE_TYPES[head]);
        }
        return elemVerbose(tail) + ' pointer';           /* head is a 'p' level */
    };

    setElem = function (rec, elem) {                     /// stamp element type on a meta record
        metaSlot(rec).elem = elem;
    };

    /* --------------------------------------------------------- *
     *  Struct layouts (Impala 2 Step 2)                          *
     * --------------------------------------------------------- */

    structWords = function (name) {                      /// total word size of a defined struct
        var s = structs[name];
        return (s && s.complete ? s.words : undefined);
    };

    structDefined = function (name) {                    /// layout is known - the SIZE may still be symbolic
        var s = structs[name];
        return !!(s && s.complete);
    };

    /* Words occupied by one field, or undefined when that is not a number Impala knows - a symbolic
       extent (`int array v[N]`) or an element struct that has one. The EMITTED layout is symbolic
       either way (see emitStructLayout), so undefined means "the assembler knows this, we do not",
       never "incomplete" - structDefined is the test for that. This used to multiply the extent
       OPERAND ('N') by a number and hand back NaN, which flowed into s.words unchecked. */
    fieldWords = function (field) {                      /// words occupied by one field
        if (field.type === 'S') {
            return structWords(field.struct);
        }
        if (field.type === 'A') {
            var count = constInt('#' + field.size);
            var per = (isStructAtom(field.elem)
                    ? structWords(field.elem) : 1);
            return (count === undefined || per === undefined) ? undefined : count * per;
        }
        return 1;                                                 /* scalar i/f/p/F */
    };

    /* ONE namespace for every top-level name. Impala used to key each kind off its own table, while GAZL
       has a single flat symbol space - so `global int S` beside `function S()` passed every check and
       then failed to assemble ("Symbol already defined: S"), and `struct S` + `functype S` was rejected
       in one order and accepted in the other. Types are not emitted under a bare name, but they are read
       by position alone, and every symbol the compiler mints is keyed on a user name - so `.z.S` would
       be both `struct S`'s size and a global array `S`'s extent, and each new family would otherwise
       need its own collision analysis, two of which were only found by accident. Claiming names once
       retires the class, and is what lets one `.z.` tag serve every size (see extentSymbol).

       Re-claiming the SAME kind is how a declaration meets its definition, and how an import closure
       sees one unit twice; the per-kind agreement checks (E401 / E410 / E437 / E438 / E440) own that
       case, so only a DIFFERENT kind is a clash here. */
    var TOP_KINDS = { globals: 'global', functions: 'function', defines: 'const' };
    claimTopName = function (name, kind, sourceCode, sourceOffset) {
        if (kind === undefined || name === undefined || name === null
                || ('' + name).charAt(0) === '.') {
            return;                                           /* compiler-minted: no Impala identifier starts with `.` */
        }
        checkReservedName(name, kind, sourceCode, sourceOffset);
        var prev = topNames[name];
        if (prev !== undefined && prev !== kind) {
            fail('Name already used by a ' + prev + ': ' + name,
                    sourceCode, sourceOffset, 'E401',
                    'every top-level name must be unique - rename the ' + kind + ' or the ' + prev);
        }
        topNames[name] = kind;
    };

    beginStruct = function (name, sourceCode, sourceOffset) {
        claimTopName(name, 'struct', sourceCode, sourceOffset);
        var prev = structs[name];
        if (prev && prev.complete && !prev.extern) {
            fail('Struct already defined: ' + name, sourceCode, sourceOffset, 'E410');
        }
        /* A completed `extern struct` is a CLAIM about a layout, not a second definition of it, so
           set it aside and let endStruct compare the two rather than rejecting the definition it
           describes. The definition wins either way. */
        structs[name] = { shadowed: (prev && prev.complete ? prev : undefined),
                fields: [], words: 0, complete: false,
                sourceCode: sourceCode, sourceOffset: sourceOffset };
        openStruct = name;
    };

    /* An `extern struct` declaration and a `struct` definition of one name are two claims about one
       layout, and must agree - the E437 rule, for types. Whichever arrived second was parsed into a
       fresh entry so the first stayed intact; the DEFINITION is authoritative in either order.
       Returns true when the entry just parsed was the DECLARATION, i.e. it owns no layout to emit. */
    checkStructAgreement = function (name) {
        var parsed = structs[name], held = parsed.shadowed;
        if (!held) {
            return false;
        }
        var parsedRow = structSignatureRow(name, false);
        structs[name] = held;
        var heldRow = structSignatureRow(name, false);
        if (parsedRow !== heldRow) {
            /* fail() bakes its message, and bake() EVALS anything between braces (that is how
               `{$type1}` interpolation works), so a struct row's `{ a : int }` must not go in raw. */
            function shown(row) { return replace(replace(replace(row, 'signature ', ''), '{', '('), '}', ')'); }
            failDisagreement('struct ' + name, shown(parsed.extern ? parsedRow : heldRow),
                    shown(parsed.extern ? heldRow : parsedRow), 'E438',
                    !(parsed.extern && held.extern),              /* both extern -> no definition to arbitrate */
                    'struct', 'layouts', parsed.sourceCode, parsed.sourceOffset);
        }
        if (!parsed.extern) {
            structs[name] = parsed;                      /* a definition outranks the declaration it fulfils */
        }
        return !!parsed.extern;
    };

    /* A field extent that needs folding lives in a `<X>` scratch that the layout block reads LATER, so
       the borrow must survive the declarator - otherwise a second such field folds into the same scratch
       and silently overwrites the first one's extent. ArrayDecl hands every extent to its consumer (see
       there); this is the struct's release, once the layout has read them. */
    endStruct = function (name) {
        var s = structs[name];
        s.complete = true;
        var redeclared = checkStructAgreement(name);
        if (!redeclared) {
            emitStructLayout(name);
        }
        var held = {};
        for (var i = 0; i < s.fields.length; ++i) {
            var size = '' + s.fields[i].size;
            if (size.charAt(0) !== '<') continue;
            assert(!held[size], 'struct ' + name + ' reuses extent scratch ' + size);
            held[size] = true;
            returnBack(s.fields[i].size);
        }
        openStruct = undefined;
        return redeclared;
    };

    /* Emit a struct's layout as GAZL compile-time constants: a rolling `<a>` accumulator that
       snapshots each field offset into `.o.Name.field` and the total size into `.z.Name`. Field
       access then references these symbols instead of baked numbers. Structs are defined in
       dependency order (a by-value field's struct must be complete first, E412), so `#.z.Inner`
       is always defined before an outer struct advances past it. Extern structs emit nothing
       (the host owns their layout). See docs/StructLayoutConstants.md. */
    emitStructLayout = function (name) {
        if (dry) return;
        if (typeof output !== 'function') return;
        var s = structs[name];
        if (!s || s.extern) return;                               /* extern: host provides all base offsets + .z */
        var T = (typeof TAB !== 'undefined') ? TAB : '\t';
        flushMetaCode('');                                   /* a field extent that folded to a `<X>` scratch was
                                                                         queued through emit(); drain it BEFORE the block so
                                                                         the definition precedes the ! ADDi that reads it
                                                                         (same rule declare() follows before its own output) */
        output(T + '! MOVi <a> #0' + T + '; layout of struct ' + name);
        for (var i = 0; i < s.fields.length; ++i) {
            var f = s.fields[i];
            output('.o.' + name + '.' + f.name + ':' + T + '! DEFi #<a>');
            if (f.type === 'S') {                                 /* nested by-value struct */
                output(T + '! ADDi <a> #<a> #.z.' + f.struct);
            } else if (f.type === 'A') {                          /* array: name the extent, THEN advance by it */
                var x = extentSymbol(f.name, name);
                var words = f.size;
                if (isStructAtom(f.elem)) {              /* count * element size, folded now */
                    output(T + '! MULi <t> #' + f.size + ' #.z.' + f.elem);
                    words = '<t>';
                }
                output(x + ':' + T + '! DEFi #' + words);
                if (constInt('#' + f.size) === undefined) {
                    /* A SYMBOLIC extent could still be negative - `const int K = -1`, or a host-supplied
                       `! DEFi`. Impala cannot see the number (ArrayDecl rejects the ones it can), and
                       nothing downstream would: this count is only ever ADDED to the offset accumulator,
                       so a negative one runs the layout BACKWARDS and lands the next field on top of an
                       earlier one, silently. Ask the assembler, in the same deferred form the index
                       checks use. Zero runtime cost, and only for a field whose extent is not a number. */
                    var neg = '.g' + (guardCounter++);
                    output(T + '! GEQi #' + x + ' #0 @' + neg);
                    output(T + '! FAIL extent of ' + name + '.' + f.name
                            + ' is negative, which would run the struct layout backwards');
                    output(neg + ':' + T + '! ADDi <a> #<a> #' + x);
                    continue;
                }
                output(T + '! ADDi <a> #<a> #' + x);
            } else {                                              /* scalar (int/float/ptr/funcptr) */
                output(T + '! ADDi <a> #<a> #1');
            }
        }
        output('.z.' + name + ':' + T + '! DEFi #<a>');
    };

    /* Fold a place's compile-time offset PARTS (field-offset symbols + constant strides) into one
       operand, emitting inline `! ADDi` (assemble-time, zero runtime cost) only when there are 2+
       parts. One part -> used bare; zero -> null (offset 0). The scratch is pool-managed (`<`),
       returned when the consuming meta is released, so simultaneously-live reads never collide. */
    foldOffset = function (parts) {
        if (!parts || parts.length === 0) return null;
        if (parts.length === 1) return parts[0];      /* a lone part (symbol or scratch) is returned as-is; its owner frees it */
        var o = borrow('<');
        emit('<> +', 'i', o, '#' + parts[0], '#' + parts[1]);
        for (var k = 2; k < parts.length; ++k) {
            emit('<> +', 'i', o, '#' + o, '#' + parts[k]);
        }
        for (var j = 0; j < parts.length; ++j) {      /* release borrowed stride scratches now folded into o */
            if (('' + parts[j]).charAt(0) === '<') returnBack(parts[j]);
        }
        return o;
    };

    /* `extern struct Name { fields }`: host-owned layout. Impala knows the interface (field
       names + types) and emits symbolic `.o.Name.field` / `.z.Name` references, but does NOT
       emit the layout - the host supplies those constants at load. See docs/StructLayoutConstants.md. */
    /* Never rejects: the body has not been parsed yet, so it is not yet known whether this
       declaration asserts anything at all. A BODYLESS `extern struct N` is the struct analogue of a
       name-only `extern function f` - an opaque handle making no layout claim - so it must not
       collide with a definition the closure already has; ExternDecl simply puts that definition
       back. A bodied one IS a claim, parsed alongside the definition and compared by endStruct. */
    beginExternStruct = function (name, sourceCode, sourceOffset) {
        claimTopName(name, 'struct', sourceCode, sourceOffset);
        var prev = structs[name];
        if (!prev || prev.complete) {
            structs[name] = { shadowed: (prev && prev.complete ? prev : undefined),
                    fields: [], words: 0, complete: false,
                    sourceCode: sourceCode, sourceOffset: sourceOffset };
        }
        structs[name].extern = true;
    };

    /* A BODYLESS `extern struct N` never reaches endStruct, so nothing compares it - and with no body
       it claims no layout, so there is nothing to compare. Give back whatever it was parsed beside. */
    cancelStructRedeclaration = function (name) {
        var s = structs[name];
        if (s && s.shadowed) {
            structs[name] = s.shadowed;
        }
    };

    isExternStruct = function (name) {
        var s = structs[name];
        return !!(s && s.extern);
    };

    /* The `*size` operand for a by-value struct ALLOCATION (LOCA / GLOB). An extern struct's real
       size is host-owned, so use the symbolic `.z.Name` (resolved at load); a normal struct uses its
       compiler-known word count. Frame/global allocation accepts a symbolic size, so by-value extern
       locals/globals work. (Passing/returning a struct BY VALUE is different - it reserves a
       compile-time-known number of transient VM slots, which a host-owned size cannot provide. By-value
       params/returns are parked wholesale today (E426/E427), so the extern-specific E425 that used to
       state this no longer fires; docs/GAZLSymbolicWindows.md is where the reasoning lives.) */
    structAllocSize = function (structName) {
        return '*.z.' + structName;                           /* always symbolic - adapts to the (possibly host/assembler-set) size */
    };

    /* The symbol naming an array's extent in WORDS - the `*size` operand, not the element count (a
       struct-element array is `count * .z.Elem`). Same tag as a struct's size because it is the same
       quantity: `.z.<path>` is the words occupied by `<path>`, whether that is `.z.Voice` (a struct),
       `.z.bank` (a global array), `.z.main.buf` (a local) or `.z.S.v` (a struct array field). One tag
       is only sound because a top-level name has exactly one kind (claimTopName) - otherwise `.z.S`
       would mean both `struct S` and a global array `S`. Struct array fields only: a scalar field is
       one word and a by-value field is `.z.Inner`, so those are already nameable.
       See docs/SymbolNamespace.md. */
    extentSymbol = function (name, owner) {
        return '.z.' + (owner !== undefined ? owner + '.' : '') + name;
    };

    /* '*size' operand for allocating an ARRAY, published as a NAMED assemble-time constant rather
       than left in a `<X>` scratch. A scratch is a recycled register: it holds the extent only until
       the next borrow, so the allocation line was the one and only place that could read it. A named
       constant is permanent and referenceable, which is what lets an extent be quoted anywhere later.
       The value is usually symbolic - a host `! DEFi` count, or `count * .z.Elem` that only resolves
       at assembly - so naming it is the ONLY way to hold on to it; there is no number to keep. A
       struct-element array is that `count * .z.Elem`, folded by a compile-time `! MULi` so the
       allocation tracks the element size exactly like the subscript stride does; scalar elements are
       one word each, so the count IS the size. declare() flushes the fold ahead of the `! DEFi` that
       reads it, and hands the scratch back. */
    arrayAllocSize = function (elemDesc, count, symbol) {
        var value = count;
        if (elemDesc !== undefined && descTail(elemDesc) === undefined
                && isStructAtom(descHead(elemDesc))) {
            value = borrow('<');
            emit('<> *', 'i', value, '#' + count, '#.z.' + descHead(elemDesc));
        }
        /* A DERIVED name must never be the one that reports a user's mistake. This runs as an ARGUMENT to
           the `declare` that registers the array itself, so JS evaluates it FIRST - and on a duplicate
           `global array g[2]` the `.z.g` collision fired before the `g` collision could, aborting with
           `Identifier already declared: .z.g` and no code, position or caret, naming a symbol the user
           never wrote. Skipping the redeclare lets the owner's own diagnostic (E401) arrive instead.
           Safe because `.z.g` derives from `g` alone: a collision here MEANS the owner collides too, and
           the stale definition dies with the compile a moment later. */
        if (symbols.defines === undefined || symbols.defines[symbol] === undefined) {
            declare('! DEF?', 'defines', symbol, 'i', true, '#' + value);
        }
        return '*' + symbol;
    };

    /* The struct atom a pointer/array element descriptor points at, or undefined for anything whose
       stride is one word. `int pointer` and untyped `pointer` both land in the undefined case, which is
       why scaling changes nothing for them. */
    strideStruct = function (elemDesc) {
        if (elemDesc === undefined || descTail(elemDesc) !== undefined) {
            return undefined;
        }
        var head = descHead(elemDesc);
        return isStructAtom(head) ? head : undefined;
    };

    /* Does a subscript on this slot stride by a struct size rather than by one word? That is exactly the
       question `[i]` vs `[[i]]` asks, so both the Subscript rule and the pointer-arithmetic diagnostics
       ask it here. Note a SCALAR array field inside a struct (`s.v[k]`) reaches subscriptStruct too but
       strides one word, so the predicate is the element type, never which code path handles it. */
    subscriptScales = function (slot) {
        var elem;
        if (slot.place && slot.arrayOf) {
            elem = slot.arrayOf;
        } else if (slot.type === 'p') {
            elem = slot.elem;
        } else {
            return false;
        }
        return strideStruct(elem) !== undefined;
    };

    /* One declarator, COPIED out of a VarDecl/ArrayDecl node for an ArgsDecl/LocalsDecl list. The copy
       is required, not tidiness: the declarator node is pooled and recycled by the parser, so anything
       holding a reference would later see whichever declarator came last. */
    declEntry = function (type, elem, struct, words, name, size) {
        return { type: type, elem: elem, struct: struct, words: words, name: name, size: size };
    };

    addStructField = function (name, field, sourceCode, sourceOffset) {
        var s = structs[name];
        for (var i = 0; i < s.fields.length; ++i) {
            if (s.fields[i].name === field.name) {
                fail('Duplicate field ' + field.name + ' in struct ' + name,
                        sourceCode, sourceOffset, 'E411');
            }
        }
        if (field.type === 'S' && !structDefined(field.struct)) {   /* DEFINED, not numerically sized -
                                                                               a symbolic size nests fine (#.z.Inner) */
            fail('Field ' + field.name + ' has incomplete struct type ' + field.struct
                    + ' (define it, or use a pointer)', sourceCode, sourceOffset, 'E412');
        }
        field.words = fieldWords(field);
        s.fields.push(field);
        s.words = (s.words === undefined || field.words === undefined)
                ? undefined : s.words + field.words;              /* one symbolic field and the total is unknown */
    };

    /* Walk a struct layout against a brace-tree of constants, producing the flat list of
       constant operands (field order); trailing/omitted slots zero-fill. Recurses for
       nested struct and array fields. `items` is an array of { op, type } | { braced:[...] }. */
    /* One entry from a brace list, in a position where the INDEX does the naming. */
    indexedEntry = function (entry, sourceCode, sourceOffset) {
        if (entry !== undefined && entry.field !== undefined) {
            fail('`' + entry.field + ':` names a field, but an array element is positional',
                    sourceCode, entry.at, 'E458', 'drop the name - the position IS the index here');
        }
        return entry;
    };

    /* Map a struct's brace entries onto its fields BY NAME, or undefined when the list is the 1.x
       positional form, which only --legacy still maps by position. Naming is the default because a
       positional list silently changes meaning the moment a field is inserted, removed or reordered
       - nothing in the source has to change for it to start initializing different fields. */
    fieldEntries = function (structName, fields, items, sourceCode, sourceOffset) {
        if (items.length === 0) {
            return undefined;                                 /* `{}` - every field zero-fills */
        }
        if (items[0].field === undefined) {
            strictError('Initializer for struct ' + structName + ' must name its fields',
                    sourceCode, items[0].at, 'E455',
                    'write `{ ' + fields[0].name + ': ... }` - a positional list changes meaning '
                            + 'whenever a field moves');
            /* Reached only under --legacy, where the positional list is mapped by index and anything
               past the last field is dropped. Named lists get E456 for a name no field has; the
               positional form has no name to report, so it needs the count rule instead. */
            if (items.length > fields.length) {
                failSurplus(structName, 'values', items.length, fields.length,
                        items[fields.length].at, sourceCode, sourceOffset);
            }
            return undefined;                                 /* --legacy: map by position */
        }
        var known = {}, byName = {}, i;
        for (i = 0; i < fields.length; ++i) {
            known['$' + fields[i].name] = true;               /* '$' prefix: a field may be named `constructor` */
        }
        for (i = 0; i < items.length; ++i) {
            var e = items[i];
            if (e.field === undefined) {
                fail('Initializer for struct ' + structName
                        + ' mixes named and positional entries', sourceCode, e.at, 'E455',
                        'name every field, or none of them');
            }
            if (known['$' + e.field] === undefined) {
                fail('struct ' + structName + ' has no field ' + e.field,
                        sourceCode, e.at, 'E456');
            }
            if (byName['$' + e.field] !== undefined) {
                fail('Field ' + e.field + ' is initialized twice',
                        sourceCode, e.at, 'E457');
            }
            byName['$' + e.field] = e;
        }
        return byName;
    };

    buildStructInit = function (structName, items, out, sourceCode, sourceOffset) {
        var fields = structs[structName].fields;
        /* The HOST owns an extern struct's field offsets and its size, so a positional DATA row is a
           guess at all three: field order, `.z.`, and whether there are fields Impala never saw. Reads
           already adapt (`POKE &g:.o.E.f`); only static data is early-bound, which is exactly why it is
           the only part that can be wrong. Emitting it anyway is the "half-deferred" shape
           docs/TwoStageConstants.md calls worse than either consistent choice. */
        if (isExternStruct(structName)) {
            blockInitFrom(out,
                    'the host owns the layout of struct ' + structName
                            + ', so Impala does not know which word it would land in', 'E459',
                    'a host-owned struct is initialized by the host - leave it zero-filled here; '
                            + 'static initialization of a host-owned layout needs GAZL 2 and is planned '
                            + 'for Impala 3.0 (docs/ParkedFeatures.md)');
        }
        var byName = fieldEntries(structName, fields, items, sourceCode, sourceOffset);
        for (var fi = 0; fi < fields.length; ++fi) {
            var f = fields[fi];
            var item = (byName !== undefined ? byName['$' + f.name]
                    : (items && fi < items.length) ? items[fi] : undefined);
            if (f.type === 'S') {
                buildStructInit(f.struct, (item && item.braced) || [], out, sourceCode, sourceOffset);
            } else if (f.type === 'A') {
                var arr = (item && item.braced) || [];
                var structEl = isStructAtom(f.elem);
                /* A symbolic extent is fillable, but only up to the values actually given, and only if
                   they FIT - an over-filled array spills into whatever follows: `v[N]` with N=2 given
                   three values, and a `z` after it, emits four words that fit `1+N+1` exactly, so the
                   assembler passes it and z gets v's third value. Impala cannot do that comparison, but
                   it can hand it to the assembler, which by then knows the extent (assertFitsExtent).
                   What stays blocked is everything AFTER the field: the words it did not fill are a
                   symbolic count, and DATA cannot skip them. (The loop used to compare against the
                   extent OPERAND, get NaN, run zero times, and emit a short row that shifted every
                   later field.) */
                var count = constInt('#' + f.size);
                var symbolic = (count === undefined);
                if (symbolic) {
                    count = arr.length;                       /* fill what was given; the rest zero-fills */
                } else if (arr.length > count) {
                    failSurplus(f.name, 'values', arr.length, count,
                            arr[count] && arr[count].at, sourceCode, sourceOffset);
                }
                var from = out.length;
                for (var e = 0; e < count; ++e) {
                    var ev = (e < arr.length)
                            ? indexedEntry(arr[e], sourceCode, sourceOffset) : undefined;
                    if (structEl) {
                        buildStructInit(f.elem, (ev && ev.braced) || [], out, sourceCode, sourceOffset);
                    } else {
                        pushInitScalar(out, ev, f.elem, f.name, sourceCode, sourceOffset);
                    }
                }
                if (symbolic && out.blocked === undefined) {
                    /* WORDS given, not elements: a struct-element array contributes .z.Elem each, and
                       the extent symbol is in words too. Nothing to assert once the row is already
                       blocked - emitInitData drops these words rather than placing them, and
                       blockInitFrom keeps the FIRST block, so both calls are no-ops past that point. */
                    assertFitsExtent(out.length - from, structName, f.name,
                            sourceCode, sourceOffset);
                    blockInitFrom(out,
                            'the extent of ' + f.name + ' is not resolved until GAZL assembly time, so '
                                    + 'Impala cannot tell which word this would land in',
                            'E454',
                            'leave every field after ' + f.name + ' zero here - ' + f.name + ' itself may '
                                    + 'be initialized, and the assembler checks that the values fit; '
                                    + 'placing a field BEHIND a symbolic extent needs GAZL 2 and is '
                                    + 'planned for Impala 3.0');
                }
            } else {
                pushInitScalar(out, item, f.type, f.name, sourceCode, sourceOffset);
            }
        }
    };

    pushInitScalar = function (out, item, type, fieldName, sourceCode, sourceOffset) {
        if (item === undefined) {
            out.push(ZEROES[type]);                      /* omitted -> zero */
            return;
        }
        if (item.braced !== undefined) {
            fail('Too many braces in initializer for field ' + fieldName,
                    sourceCode, sourceOffset, 'E422');
        }
        if (item.type !== type) {
            typeError('Initializer type mismatch for field ' + fieldName + ' ({$type1} vs {$type2})',
                    sourceCode, sourceOffset, item.type, type, 'E422');
        }
        /* This is the ONLY place a word enters the row, so it is where "no word past here is placeable"
           belongs: here the entry still knows its own field name and source position, whereas by
           emitInitData it is a bare operand string in a flat array. Zero is exempt - the region fills
           with it under any layout, so those words are simply dropped. */
        var b = out.blocked;
        if (b !== undefined && !isZeroWord(item.op)) {
            fail('Cannot initialize ' + fieldName + ': ' + b.why,
                    sourceCode, (item.at !== undefined ? item.at : sourceOffset), b.code, b.hint);
        }
        out.push(item.op);
    };

    /* Mark every word from here on UNPLACEABLE: Impala cannot say which GAZL word it would land in.
       `DATA` is positional and GAZL 1 cannot skip a symbolic number of words - there is no fill
       directive, and a backward `! GOTO` does not assemble (`Compile time label not found`), so no
       assemble-time loop can emit them either. Such words are only harmless when they are ZERO, which is
       what the region fills them with anyway, so emitInitData requires that and then drops them. Two
       situations reach this and they share everything but the message: a symbolically-sized array field
       (E454), and a host-owned `extern struct` layout (E459). */
    /* Surplus initializer entries are dropped by every fill loop (they stop at the extent), so they
       vanish with nothing wrong for the assembler to see. Both loops - a struct's array FIELD and a
       global array OF structs - need the identical rule, so it lives here once. `at` is the surplus
       entry's own position, so the caret lands on it rather than on the initializer's `{`. */
    failSurplus = function (name, what, given, holds, at, sourceCode, sourceOffset) {
        fail('Too many initializer ' + what + ' for ' + name + ': ' + given
                + ' given, but it holds ' + holds,
                sourceCode, (at !== undefined ? at : sourceOffset), 'E460',
                'remove the extra ' + (given - holds) + ', or widen ' + name);
    };

    /* Hand a comparison Impala cannot make to the stage that can: `words <= .z.Struct.field`, deferred
       to GAZL assembly time, where the extent finally has a value. Rule 4 of docs/TwoStageConstants.md,
       and it costs nothing at run time - every line is an `!` directive, so no word is emitted either
       way.

       `! LEQi` + `! FAIL` + a skip label is the CANONICAL idiom - docs/TwoStageConstants.md prescribes
       it and src/UnitTest.gazl:30-40 guards GAZL_VERSION exactly this way. An earlier version of this
       branched to a deliberately UNDEFINED label so the label name doubled as the message; that is the
       trick TwoStageConstants.md and CompileTimeHardening.md both explicitly ban, because an identifier
       cannot carry spaces - so it could not name the two counts that make the message actionable.

       It must sit ABOVE the rows it guards. GAZL checks only the WHOLE allocation (`Not enough space in
       data section: s`), so an over-filled FIELD that still fits the struct total spills into the next
       field and assembles silently; and where the total DOES overflow, whichever check comes first in
       the file wins, so emitting below the rows would trade this message for the coarser one. Verified
       against GAZLCmd on 2026-08-02: words == extent passes, both over-fill shapes fail here. */
    assertFitsExtent = function (words, structName, fieldName, sourceCode, sourceOffset) {
        if (words <= 0) {
            return;                                           /* nothing given - trivially fits */
        }
        var extent = extentSymbol(fieldName, structName);
        assembleAssert([['LEQ?', '#' + words + ' #' + extent]],
                'too many initializer values for ' + structName + '.' + fieldName + ': '
                        + words + ' given, room for ' + extent, sourceCode, sourceOffset);
    };

    /* The mnemonic for one flat-array initializer row. `DATi`/`DATf`/`DATp` apply their type to EVERY
       operand on the line (`src/GAZL.cpp:996-1019` - one loop shared by all four, `accepts` picked from
       the 4th mnemonic character), so the assembler re-checks what Impala already checked. That second
       line of defence is the point: while these rows were `DATA` - which takes `KONST`, anything at all -
       Impala was the ONLY check, which is how a tautological one went unnoticed. It also covers what
       Impala cannot fold: `DATi #N` verifies that `N` really is `! DEFi` and not `! DEFf`.

       `DATA` stays for an UNTYPED array, which states no element type to check against. Struct
       initializers keep it too, and must: one row spans fields of different types (`DATA #1 #2.5 &s`),
       which is exactly the mixed case `DATA` exists for - see `consts.mixed` in src/UnitTest.gazl. */
    initElemRow = function () {
        return (initElemType !== undefined ? 'DAT?' : 'DATA');
    };

    blockInitFrom = function (out, why, code, hint) {
        if (out.blocked === undefined) {                      /* the FIRST block bounds the row */
            out.blocked = { at: out.length, why: why, code: code, hint: hint };
        }
    };

    /* Zero is the one word that is safe to drop, because the region zero-fills to it under any layout -
       so test the VALUE, not the spelling. `0x0`, `-0`, `0.0e0` and `&NULL` are all zero; comparing
       against the canonical ZEROES strings rejected safe code that merely wrote one of them differently.
       A symbol (`#N`) is NOT zero as far as Impala is concerned - it does not know the value, and per
       docs/TwoStageConstants.md it must not guess one. */
    isZeroWord = function (operand) {
        if (typeof operand !== 'string') {
            return false;
        }
        if (operand === '&NULL') {
            return true;                                      /* null pointer / nullfunc */
        }
        var v = (operand.charAt(0) === '#' ? operand.substr(1) : '');
        return v !== '' && Number(v) === 0;                   /* NaN for a symbol -> not zero */
    };

    /* emit a flat constant list as one or more DATA rows (mirrors InitList chunking) */
    emitInitData = function (ops, sourceCode, sourceOffset) {
        if (ops.blocked !== undefined) {
            ops.length = ops.blocked.at;                      /* the region zero-fills the remainder;
                                                                 pushInitScalar already refused any
                                                                 non-zero word past this point */
        }
        var line = '';
        for (var i = 0; i < ops.length; ++i) {
            if (line !== '' && (line + ' ' + ops[i]).length >= 55) {
                declare('DATA', 'globals', undefined, 'i', true, line, sourceCode, sourceOffset);
                line = '';
            }
            line += (line === '' ? '' : ' ') + ops[i];
        }
        if (line !== '') {
            declare('DATA', 'globals', undefined, 'i', true, line, sourceCode, sourceOffset);
        }
    };

    /* record one `returns` declarator on a function entry (Step 4: multiple return values,
       slice 2.5: by-value struct returns) */
    addReturn = function (entry, rawName, type, elem, size, sourceCode, sourceOffset, struct, words) {
        if (!entry.pendingReturns) {
            entry.pendingReturns = [];
        }
        entry.pendingReturns.push({
            name: '$' + rawName,
            rawName: rawName,
            type: type,
            elem: elem,
            size: (size !== undefined ? '*' + size : undefined),
            struct: struct,
            words: words,
            sourceCode: sourceCode,
            sourceOffset: sourceOffset
        });
    };

    findField = function (structName, fieldName) {
        var s = structs[structName];
        if (!s) return undefined;
        for (var i = 0; i < s.fields.length; ++i) {
            if (s.fields[i].name === fieldName) return s.fields[i];
        }
        return undefined;
    };

    /* A place names a struct (or struct array) at base + a sum of compile-time offset parts. Only a
       terminal scalar field emits code, so struct values never enter the word temporaries. */
    setPlace = function (rec, baseKind, base, offParts, structName, root, arrayOf, dynIndex) {
        var slot = metaSlot(rec);
        slot.operator = '@place';
        slot.operands = [ undefined, undefined, undefined ];
        slot.place    = true;
        slot.baseKind = baseKind;                                 /* 'local' | 'pointer' | 'globalAddr' */
        slot.base     = base;
        slot.offParts = offParts || [];
        slot.arrayOf  = arrayOf;                                  /* set -> an array of `arrayOf`; a later [k] indexes it */
        slot.struct   = arrayOf ? undefined : structName;
        slot.type     = arrayOf ? 'p' : 'S';
        slot.root     = root || structName || arrayOf;
        slot.elem     = arrayOf || structName;
        slot.dynIndex = dynIndex;                                 /* frame place + one runtime word-index -> terminal emits GETL/SETL */
        slot.extent   = undefined;                                /* pooled slot: never inherit another array's extent or
                                                                     another subscript's finding. The two callers that DO
                                                                     have an extent assign it after the call. */
        slot.oobIndex = undefined;
    };

    /* A place resolved to a terminal SCALAR location (a scalar field, or a scalar array element) becomes a
       value meta: local -> frame-relative MOV (GETL/SETL when a runtime index is present), global -> MOV*,
       pointer -> PEEK/POKE base <index-or-#offset>. `parts` are the compile-time offset parts to fold. */
    emitPlaceValue = function (x, bk, base, parts, dynIndex, type, elemTail) {
        var offOp = foldOffset(parts);
        /* Writability is carried by the OPERATOR here, exactly as it is for a scalar global (`:=*` has no
           assign() branch). A field of a `readonly` struct used to reach this as a plain `=*`, so the
           POKE was emitted and only the CNST region caught it, at load. */
        var ro = (x.readonly === true);
        if (bk === 'local' && dynIndex !== undefined) {           /* (dsp + base:off)[dynIndex] */
            makeMeta(x, '=[]$', type, null, base + (offOp ? ':' + offOp : ''), dynIndex);
        } else if (bk === 'local') {
            makeMeta(x, '=', type, undefined, base + ':' + offOp, undefined);
        } else if (bk === 'globalAddr') {                         /* &name:off in global memory */
            makeMeta(x, (ro ? ':=*' : '=*'), type, undefined, base + ':' + offOp, undefined);
        } else {                                                  /* pointer base: PEEK/POKE base <runtime index | #offset> */
            makeMeta(x, '=[]', type, null, base, dynIndex !== undefined ? dynIndex : '#' + offOp);
        }
        x.place = false;
        x.type  = type;
        x.elem  = elemTail;
        x.readonly = ro;                                          /* survives the makeMeta above, which
                                                                     clears it - assign() reads the FLAG */
    };

    /* structPtr[k] or arrayField[k] -> the element place. A constant index adds a symbolic stride to the
       offset parts (folds into the following .field, no runtime cost); a dynamic index materializes. */
    subscriptStruct = function (x, idx, sourceCode, sourceOffset) {
        x = metaSlot(x);
        var extent = x.extent;
        if (!x.arrayOf) {                                         /* a raw struct pointer: wrap it as an array place */
            var p = makeRValue(x);
            setPlace(x, (p[0] === '&' ? 'globalAddr' : 'pointer'), p, [], undefined, x.elem, x.elem);
        }
        var elem = x.arrayOf;
        var elemStruct = isStructAtom(elem);            /* struct element -> stride .z.elem, a place for the next .field;
                                                                    scalar element -> stride 1 word, this [k] IS the terminal value */
        var eType = descHead(elem), eTail = descTail(elem);
        var idxRV = makeRValue(metaSlot(idx));

        var oobIndex = checkConstIndex(extent, idxRV, sourceCode, sourceOffset);

        var idxKind = indexKind(idxRV);
        if (idxKind !== 'runtime') {                             /* an assemble-time index -> fold into the compile-time offset parts */
            /* This test used to be `/^#[0-9]+$/`, so a named const, a NEGATIVE literal and a folded `<A>`
               all fell to the branch below and emitted GETL/SETL with an immediate index - an encoding
               that does not exist, so a module the compiler accepted would not assemble. */
            var scratch = (idxKind === 'scratch');
            var k = dropHash(idxRV);
            if (!scratch) {
                returnBack(idxRV);                      /* a scratch passes to offParts, whose fold frees it */
            }
            if (k !== '0') {
                var part = k;                                    /* scalar stride is 1 word -> the offset is just k */
                if (elemStruct) {
                    part = borrow('<');
                    emit('<> *', 'i', part, '#' + k, '#.z.' + elem);
                    if (scratch) { returnBack(k); }
                }
                /* A folded `<X>` cannot key a deferred assertion, so the guard takes its OWN copy while the
                   value is still live - the pushed one is freed by foldOffset long before the use decides
                   whether this is a dereference at all. One assemble-time MOVi, no runtime cost, and the
                   copy is returned by whichever side consumes the finding. */
                if (scratch && extent !== undefined && extent.inField) {
                    var g = borrow('<');
                    emit('<> =', 'i', g, '#' + part, undefined);
                    oobIndex = { k: g, own: true, ext: extent, copyAt: metacode.length - 1,
                            src: sourceCode, off: sourceOffset };
                }
                x.offParts.push(part);
            }
            if (elemStruct) setPlace(x, x.baseKind, x.base, x.offParts, elem, x.root, undefined, x.dynIndex);
            else            emitPlaceValue(x, x.baseKind, x.base, x.offParts, x.dynIndex, eType, eTail);

        } else if (x.baseKind === 'local' && x.dynIndex === undefined) {
            /* a frame place with a single runtime index: keep it frame-relative so it emits one
               GETL/SETL (dsp + constOff)[idx], not ADRL + ADDp + PEEK/POKE (struct index is scaled;
               scalar is 1). Only a genuinely RUNTIME index reaches here - every assemble-time one, named
               or negative, folded above, because GETL/SETL have no immediate-index form. */
            if (elemStruct) {
                var frameIdx = borrow('%');
                emit('*', 'i', frameIdx, idxRV, '#.z.' + elem);
                emitRangeCheck(frameIdx, extent, sourceCode, sourceOffset);   /* scaled: `.z.` counts words */
                returnBack(idxRV);
                setPlace(x, 'local', x.base, x.offParts, elem, x.root, undefined, frameIdx);
            } else {
                emitRangeCheck(idxRV, extent, sourceCode, sourceOffset);
                emitPlaceValue(x, 'local', x.base, x.offParts, idxRV, eType, eTail);
            }

        } else {
            var arrPtr = placeAddress(x);                /* pointer base or a second runtime index: materialize */
            if (elemStruct) {
                var elemPtr = borrow('%'), scaled = borrow('%');
                emit('*', 'i', scaled, idxRV, '#.z.' + elem);
                emitRangeCheck(scaled, extent, sourceCode, sourceOffset);
                emit('+', 'p', elemPtr, arrPtr, scaled);
                returnBack(scaled);
                returnBack(idxRV);
                returnBack(arrPtr);
                setPlace(x, 'pointer', elemPtr, [], elem, elem);
            } else {                                              /* scalar stride 1 -> PEEK/POKE arrPtr idx directly */
                emitRangeCheck(idxRV, extent, sourceCode, sourceOffset);
                emitPlaceValue(x, 'pointer', arrPtr, [], idxRV, eType, eTail);
            }
        }
        x.oobIndex = oobIndex;                                    /* AFTER the terminal call on every path -
                                                                     setPlace and emitPlaceValue both clear it */
    };

    /* a place's address: fold its offset parts into the base - ADRL for a local, ADDp for a pointer. */
    placeAddress = function (place) {
        place = metaSlot(place);
        var off = foldOffset(place.offParts);
        var a;
        if (place.baseKind === 'local') {                         /* size hint = the pointed-at sub-object, not the enclosing frame */
            var sz = (place.struct && isStructAtom(place.struct)) ? structAllocSize(place.struct) : '*0';
            a = borrow('%');
            emit('=&', 'p', a, place.base + (off ? ':' + off : ''), sz);
            if (place.dynIndex !== undefined) {                   /* fold the frame place's runtime index in (GETL/SETL fallback) */
                emit('+', 'p', a, a, place.dynIndex);
                returnBack(place.dynIndex);
                place.dynIndex = undefined;
            }
        } else {                                                  /* pointer / globalAddr */
            if (!off) return place.base;
            a = borrow('%');
            emit('+', 'p', a, place.base, '#' + off);
        }
        if (off && ('' + off).charAt(0) === '<') returnBack(off);
        return a;
    };

    /* by-value struct argument: reserve `words` window slots at %winSlot and COPY the
       struct value in (experiment-verified: ADRL the window region, ADRL the source, COPY). */
    copyStructArg = function (argMeta, winSlot, words) {
        argMeta = metaSlot(argMeta);
        /* A struct-return result already sits in transient window slots. Nested calls slide
           their windows so the inner result lands exactly where this argument belongs - then
           there is nothing to copy (and self-copy would be undefined behaviour): adopt it. */
        if (argMeta.winBase === winSlot && argMeta.winWords === words) {
            returnBack(argMeta.base);                    /* free the window-address pointer temp */
            argMeta.winBase = undefined;                          /* slots are now the argument window (freed by the call) */
            argMeta.winWords = undefined;
            return;
        }
        for (var k = 0; k < words; ++k) {                         /* reserve the window slots */
            claimSlot(winSlot + k);
        }
        /* Size hints here stay NUMERIC on purpose (not *.z.Struct): the window is a fixed `words`-slot
           block baked by the register allocator, and these must match it exactly - a symbolic size that
           later resolved differently would overrun the window. By-value locks the size; an extern struct,
           whose size is host-owned, could not supply one - which is why by-value and extern were mutually
           exclusive before by-value was parked entirely. See docs/GAZLSymbolicWindows.md. */
        var dst = borrow('%');
        emit('=&', 'p', dst, '%' + winSlot, '*' + words);   /* ADRL address of the window region */
        var src = placeAddress(argMeta);                    /* address of the source struct */
        emit('copy', '?', dst, src, '*' + words);           /* COPY dst src *words (matches the window) */
        returnBack(src);
        returnBack(dst);
        freeStructWindow(argMeta);                          /* if the source was a struct-return window, release it */
    };

    /* A struct-return call result lives in transient output-window slots that must be freed
       once the value has been consumed (copied out / read). Named-place operands (locals,
       globals, pointers) carry no window and are ignored. */
    freeStructWindow = function (place) {
        place = metaSlot(place);
        if (place.winBase !== undefined) {
            for (var i = place.winWords - 1; i >= 0; --i) {
                returnBack('%' + (place.winBase + i));
            }
            place.winBase = undefined;
            place.winWords = undefined;
        }
    };

    /* fp->field, place.field, nested chains - Step 2 slices 1-2 */
    fieldAccess = function (x, fieldName, arrow, sourceCode, sourceOffset) {
        x = metaSlot(x);

        var bk, base, offParts, structName, root, dynIndex;
        if (x.place) {
            if (x.winBase !== undefined) {
                fail("Cannot access a field directly on a returned struct value",
                        sourceCode, sourceOffset, 'E423',
                        'assign the call result to a local first, then read its fields');
            }
            if (arrow) {
                fail("Use '.' - this is a struct value, not a pointer", sourceCode,
                        sourceOffset, 'E416', 'write ' + fieldName + ' as .' + fieldName);
            }
            bk = x.baseKind; base = x.base; offParts = x.offParts || []; structName = x.struct; root = x.root;
            dynIndex = x.dynIndex;
        } else if (x.type === 'p' && isStructAtom(x.elem)) {
            if (!arrow) {
                fail("Use '->' to access a field through a pointer", sourceCode,
                        sourceOffset, 'E416', 'write ' + fieldName + ' as ->' + fieldName);
            }
            bk = 'pointer'; base = makeRValue(x); offParts = []; structName = x.elem; root = x.elem;
        } else {
            fail("Field access requires a struct" + (arrow ? ' pointer' : ''),
                    sourceCode, sourceOffset, 'E415');
        }

        checkIndexUse(x);                                 /* `e[[9]].a` reaches INTO an element that is not there */

        var field = findField(structName, fieldName);
        if (!field) {
            fail('Struct ' + structName + ' has no field ' + fieldName,
                    sourceCode, sourceOffset, 'E417');
        }
        /* The place carries a list of compile-time offset PARTS (field-offset symbols). A field just
           appends its `.o.<struct>.<field>` part - nested structs accumulate parts with ZERO
           instructions; the parts are folded (inline `! ADDi`, assemble-time) into one operand only at
           a terminal access. Only accessed paths cost anything, and never at run time. */
        var fieldSym = '.o.' + structName + '.' + fieldName;
        var newParts = offParts.concat([fieldSym]);

        if (field.type === 'S') {                                 /* nested struct -> accumulate the part, same base, NO instruction */
            setPlace(x, bk, base, newParts, field.struct, root, undefined, dynIndex);
            return;
        }
        if (field.type === 'A') {                                 /* array field -> a fold-able array place for the next [k]
                                                                     (struct OR scalar element; subscriptStruct terminates it) */
            setPlace(x, bk, base, newParts, undefined, root, field.elem, dynIndex);
            x.extent = field.extent || (field.extent =             /* built once per FIELD, not per access */
                    { n: field.size, what: structName + '.' + fieldName,
                      sym: extentSymbol(fieldName, structName), inField: true,
                      stride: isStructAtom(field.elem) ? '.z.' + field.elem : undefined });
            return;
        }

        /* terminal scalar field */
        emitPlaceValue(x, bk, base, newParts, dynIndex, field.type, field.elem);
    };

    checkPtrAssign = function (leftx, rightx, sourceCode, sourceOffset) {
        if (leftx.type === 'p' && leftx.elem !== undefined) {     /* typed pointer target: assume loudly */
            if (rightx.operands && rightx.operands[1] === '&NULL'
                    && rightx.operands[2] === undefined) {
                return;                                           /* null is assignable to any pointer type */
            }
            if (rightx.elem !== leftx.elem) {
                fail('Pointer element type mismatch (expected '
                        + elemVerbose(leftx.elem) + ' elements, got '
                        + elemVerbose(rightx.elem) + ' elements)',
                        sourceCode, sourceOffset, 'E201',
                        'use a cast: (' + elemVerbose(leftx.elem) + ' pointer)');
            }
        }
        if (leftx.type === 'F' && isFuncTypeAtom(leftx.elem)) {   /* named funcptr type target */
            checkFuncPtrTarget(leftx.elem, bareOperand(rightx), rightx.type, rightx.elem,
                    '', sourceCode, sourceOffset);
        }
    };

    /* The lone operand of a one-operand meta - a direct `&f` / `^f` reference or `&NULL` - and
       undefined for anything compound, which is neither. */
    function bareOperand(x) {
        return (x.operands && x.operands[2] === undefined ? x.operands[1] : undefined);
    }

    /* A named funcptr target, at an assignment or at an argument (`at` names which): `nullfunc` suits
       any type, a direct function reference is checked against it, and anything else must ALREADY
       carry that exact type. An untyped funcptr is not silently promoted, for the same reason a bare
       `pointer` is not assignable to an `int pointer` (E201) - it guarantees nothing about the shape
       of what gets called. The cast that says "I checked" needs no `pointer`, a functype being one. */
    checkFuncPtrTarget = function (expected, operand, actualType, actualElem, at,
                                            sourceCode, sourceOffset) {
        if (operand === '&NULL') {
            return;
        }
        /* Gated on the LOOKUP, not on the sigil: reading a global spells itself `&name` too, so
           `&`/`^` alone cannot tell a function reference from one, and testing the sigil sent every
           global funcptr into this branch to find no function and silently fall out - past the
           element check below, which is the one that applies to it. */
        var fe = (operand && (operand.charAt(0) === '&' || operand.charAt(0) === '^')
                ? symbols.functions[operand.substr(1)] : undefined);
        if (fe && fe.signature) {                                 /* a direct function reference */
            if (!funcTypeMatches(expected, fe.signature)) {
                fail('Function ' + operand.substr(1) + " does not match funcptr type '"
                        + expected + "'" + at, sourceCode, sourceOffset, 'E441',
                        'check the parameter and return types against ' + expected);
            }
        } else if (actualType === 'F' && actualElem !== expected) {
            fail('Funcptr type mismatch' + at + " (expected '" + expected + "', got "
                    + (actualElem !== undefined && isFuncTypeAtom(actualElem)
                            ? "'" + actualElem + "'" : 'an untyped funcptr') + ')',
                    sourceCode, sourceOffset, 'E441', 'use a cast: (' + expected + ')');
        }
    };

    /* --------------------------------------------------------- *
     *  Binary operations ( + - * / [] etc. )                    *
     * --------------------------------------------------------- */
    binaryOp = function (operator, leftx, rightx,
                                  sourceCode, sourceOffset) {

        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        var lelem = leftx.elem;                                   /* element type of the base (Impala 2) */
        var lro   = leftx.readonly;                               /* a readonly array's element is readonly too */

        /* a binary result is never a verbatim call result; drop callInfo so
           assign() cannot mistake it for one (see unaryOp for rationale). */
        leftx.callInfo  = undefined;
        rightx.callInfo = undefined;

        /* validate operand-type combination */
        var sig = operator + leftx.type + rightx.type;
        var tp  = SUPPORTED_OPS[sig];
        if (tp === undefined) {
            typeError(
                'Invalid types ({$type1} and {$type2})',
                sourceCode, sourceOffset,
                leftx.type, rightx.type
            , 'E301');
        }

        /* special treatment for indexed “=[]” form */
        if (operator === '=[]') {
            var op1 = leftx.operands[1];     /* base pointer/address */
            /* A plain array reaches here rather than subscriptStruct, because it decayed to a pointer at
               lookup - so the extent rides on the meta instead of on a place. Same rule as there: only a
               DYNAMIC index gets the runtime test. A constant one is already an assemble-time offset and
               GAZL rejects it outright ("Offset out of bounds"), which is a better diagnostic than a trap.
               The two branches below are exactly the two shapes that name a declared array, `&global` and
               `$local`; anything else is a bare pointer with no extent to check. */
            var xt = leftx.extent;
            var xOob;

            if (leftx.operator === ':=' && op1[0] === '&') {

                var op2 = makeRValue(rightx);
                xOob = checkSubscript(xt, op2, sourceCode, sourceOffset);

                if (op2[0] === '#') {
                    makeMeta(
                        leftx, '=*', tp, null,
                        op1 + ':' + op2.substr(1), null
                    );
                } else if (op2[0] === '<') {
                    makeMeta(leftx, '=*', tp, null,
                                      op1 + ':' + op2, null);
                } else {
                    makeMeta(leftx, '=[]', tp, null, op1, op2);
                }

            } else if (leftx.operator === '=&') {
                assert(op1[0] === '$', "=& expects local '$'");

                var op2b = makeRValue(rightx);
                xOob = checkSubscript(xt, op2b, sourceCode, sourceOffset);

                if (op2b[0] === '#') {
                    makeMeta(leftx, '=', tp, null,
                                      op1 + ':' + op2b.substr(1), null);
                } else if (op2b[0] === '<') {
                    makeMeta(leftx, '=', tp, null,
                                      op1 + ':' + op2b, null);
                } else {
                    makeMeta(leftx, '=[]$', tp, null, op1, op2b);
                }

            } else {
                /* general indexed read */
                makeMeta(leftx, operator, tp, null,
                                  makeRValue(leftx),
                                  makeRValue(rightx));
            }
            leftx.oobIndex = xOob;                    /* after the makeMeta above, which clears it */

        } else {

            /* pointer-difference special-case “d” */
            var diff = (operator === '-' && rightx.type === 'p');
            if (diff) {
                operator = 'd';
                /* The difference counts ELEMENTS (DIFp, then DIVi by the stride), so it only means
                   anything when both pointers walk the same element type - `ip - fp` would divide a
                   float-strided span by the int size. Same rule as assignment (E201); comparison is
                   left alone, it reads a raw address either way. */
                if (lelem !== rightx.elem) {
                    fail('Pointer difference needs matching element types ('
                            + elemVerbose(lelem) + ' and '
                            + elemVerbose(rightx.elem) + ')',
                            sourceCode, sourceOffset, 'E201',
                            'subtract pointers into the same array, or cast one to match');
                }
            }

            /* Arithmetic on a struct pointer is REJECTED, because `+` and `-` carry no marker for the
               multiply they would need, and the scaled subscript already spells it: `&p[[i]]`. Scaling
               them instead was tried and reverted - it leaked into comparison (no unit there at all) and
               `for` could not honour it, `FORp` having no room for a stride. See docs/Impala2Review.md,
               "the scaled subscript is spelled `[[ ]]`". Comparisons fall through untouched: they are
               unit-free and are what the `while` walk is built on. */
            var stride = (leftx.type === 'p' ? strideStruct(lelem) : undefined);
            if (stride !== undefined && (operator === '+' || operator === '-')) {
                fail('Arithmetic on a ' + stride + ' pointer', sourceCode, sourceOffset,
                        'E307', 'a struct pointer moves by scaled subscript only - write `&p[[i]]`');
            }
            if (stride !== undefined && diff) {
                fail('Difference between ' + stride + ' pointers', sourceCode, sourceOffset,
                        'E308', 'the element count is `((pointer)q - (pointer)p) / sizeof('
                                + stride + ')`');
            }
            makeMeta(leftx, operator, tp, null,
                              makeRValue(leftx), makeRValue(rightx));
        }

        leftx.readonly = lro;                                     /* survives the makeMeta calls above */

        /* element-type propagation (Impala 2) */
        if (operator === '=[]' && lelem !== undefined) {
            var _eh = descHead(lelem);                   /* typed element read: no cast needed */
            var _et = descTail(lelem);
            if (_et === undefined && isFuncTypeAtom(_eh)) {
                leftx.type = 'F';                                 /* funcptr-array element carries its funcptr type */
                leftx.elem = _eh;
            } else {
                leftx.type = _eh;
                leftx.elem = _et;
            }
        } else if ((operator === '+' || operator === '-') && leftx.type === 'p') {
            leftx.elem = lelem;                                   /* pointer arithmetic preserves element type */
        } else {
            leftx.elem = undefined;
        }
    };


    /* --------------------------------------------------------- *
     *  Multiplication / division with special int-to-float case *
     * --------------------------------------------------------- */
    mulDivOp = function (operator, leftx, rightx,
                                  sourceCode, sourceOffset) {

        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        var sig = operator + leftx.type + rightx.type;
        var tp  = SUPPORTED_OPS[sig];
        if (tp === undefined) {
            typeError('Invalid types ({$type1} and {$type2})',
                               sourceCode, sourceOffset,
                               leftx.type, rightx.type, 'E301');
        }

        /* detect (itof X) * 1.0 -> itof */
        if (operator === '*' && leftx.operator === '=itof'
            && leftx.operands[2] === '#1.0') {

            var rightOp1Prefix = (rightx.operands[1] ? rightx.operands[1][0] : '');
            var rightOp2Prefix = (rightx.operands[2] ? rightx.operands[2][0] : '');
            var t  = rightOp1Prefix + rightOp2Prefix;
            var ok = (t.length > 0 && span(t, '#<') === t.length);

            if (ok) {
                makeMeta(
                    leftx, '=itof', 'f', null,
                    leftx.operands[1],
                    makeRValue(rightx)
                );
                leftx.elem = undefined;
                return;
            }
        }

        /* mirror case: right side is itof */
        if (operator === '*' && rightx.operator === '=itof'
            && rightx.operands[2] === '#1.0') {

            var leftOp1Prefix = (leftx.operands[1] ? leftx.operands[1][0] : '');
            var leftOp2Prefix = (leftx.operands[2] ? leftx.operands[2][0] : '');
            var t2 = leftOp1Prefix + leftOp2Prefix;
            var ok2 = (t2.length > 0 && span(t2, '#<') === t2.length);

            if (ok2) {
                makeMeta(
                    leftx, '=itof', 'f', null,
                    rightx.operands[1],
                    makeRValue(leftx)
                );
                leftx.elem = undefined;
                return;
            }
        }

        /* default multiply / divide / mod path */
        makeMeta(
            leftx, operator, tp, null,
            makeRValue(leftx),
            makeRValue(rightx)
        );
        leftx.elem = undefined;
    };


    /* --------------------------------------------------------- *
     *  Assignment helper                                        *
     * --------------------------------------------------------- */
    assign = function (x, leftx, rightx,
                                sourceCode, sourceOffset) {

        x      = metaSlot(x);
        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);
        checkIndexUse(leftx);                             /* writing it: a target is never an address */
        checkIndexUse(rightx);                            /* `x = s.v[2]` reuses the operand without makeRValue */

        /* whole-struct assignment: one COPY *sizeof, statement value is the dest place */
        if (leftx.type === 'S' || rightx.type === 'S') {
            if (leftx.type !== 'S' || rightx.type !== 'S') {
                fail('Whole-struct assignment needs a struct value on both sides',
                        sourceCode, sourceOffset, 'E420');
            }
            if (leftx.struct !== rightx.struct) {
                fail('Struct type mismatch in assignment (' + leftx.struct + ' = '
                        + rightx.struct + ')', sourceCode, sourceOffset, 'E420');
            }
            /* This path returns before the scalar `readonly` check below, so a whole-struct COPY into a
               `readonly` global used to be emitted straight into the const region. */
            if (leftx.readonly === true) {
                fail('Cannot assign to a readonly value', sourceCode, sourceOffset, 'E404',
                        'declare it `global` instead of `readonly` if it has to be written');
            }
            var savedBK = leftx.baseKind, savedBase = leftx.base,
                savedParts = leftx.offParts, savedStruct = leftx.struct, savedRoot = leftx.root;
            var dst = placeAddress(leftx);
            var src = placeAddress(rightx);
            makeMeta(x, 'copy', '?', dst, src, structAllocSize(leftx.struct));
            emitMeta(x);
            returnBack(src);
            returnBack(dst);
            freeStructWindow(rightx);                    /* release a struct-return window once copied out */
            setPlace(x, savedBK, savedBase, savedParts, savedStruct, savedRoot);
            return;
        }

        if (!leftx || leftx.operator === undefined) {
            throw new Error('JSPEG meta missing for assignment: ' + JSON.stringify(leftx));
        }

        var lop   = leftx.operator;
        var keep  = 2;          /* operand index to keep for r-value */

        if (leftx.type !== '?' && rightx.type === '?' && rightx.callInfo && rightx.callInfo.name) {
            expectFunctionReturnType(rightx.callInfo.name, leftx.type, sourceCode, sourceOffset);
            rightx.type = leftx.type;
            updateCallExpectationComment(rightx.callInfo, leftx.type);
        }

        if (leftx.type !== '?' && rightx.type !== '?' && leftx.type !== rightx.type) {
            typeError(
                /* Reads as a statement about the assignment, not as an equation: `(pointer = funcptr)`
                   left people working out which side was which. Source first, destination second. */
                'Incompatible types for assignment: cannot assign {$type2} to {$type1}',
                sourceCode, sourceOffset,
                leftx.type, rightx.type
            , 'E303');
        }

        checkPtrAssign(leftx, rightx, sourceCode, sourceOffset);

        /* `readonly` was honoured for scalars and silently ignored for an indexed write, which left the
           assembler to catch it as `Incompatible types` against the const region - if at all. The FLAG
           is the whole test: every readonly branch (scalar global, array element, struct field) sets it,
           and nothing else does, so a literal or a function name falls through to `Invalid lvalue`. */
        if (leftx.readonly === true) {
            fail('Cannot assign to a readonly value',
                    sourceCode, sourceOffset, 'E404',
                    'declare it `global` instead of `readonly` if it has to be written');
        }

        /* fast path: constant expression on RHS */
        var op1 = rightx.operands[1];
        var op2 = rightx.operands[2];
        var constPair  = (op1 ? op1[0] : '') +
                         (op2 ? op2[0] : '');
        var rhsConst   = (span(constPair, '#<') === constPair.length);

        if (lop === '=' && rhsConst) {

            makeMeta(
                x, ':=', rightx.type,
                leftx.operands[1],
                makeRValue(rightx),
                null
            );
            keep = 1;

        } else if (lop === '=') {

            makeMeta(
                x, rightx.operator, rightx.type,
                leftx.operands[1],
                rightx.operands[1],
                rightx.operands[2]
            );
            keep = ((x.operator === '=' || x.operator === ':=') ? 1 : 0);

        } else if (lop === '=*') {

            makeMeta(
                x, '*=', rightx.type,
                leftx.operands[1],
                makeRValue(rightx),
                null
            );
            keep = 1;

        } else if (lop === '=[]') {

            makeMeta(
                x, '[]=', rightx.type,
                leftx.operands[1],
                leftx.operands[2],
                makeRValue(rightx)
            );

        } else if (lop === '=[]$') {

            makeMeta(
                x, '[]$=', rightx.type,
                leftx.operands[1],
                leftx.operands[2],
                makeRValue(rightx)
            );

        } else {
            fail('Invalid lvalue', sourceCode, sourceOffset, 'E404');
        }

        /* push the instruction just built */
        emitMeta(x);

        /* release all temporaries except the one we keep */
        for (var i = 2; i >= 0; --i) {
            if (i !== keep) {
                returnBack(x.operands[i]);
            }
        }

        /* finally generate r-value of assignment */
        makeMeta(
            x, '=', x.type,
            null,
            x.operands[keep],
            null
        );
    };

    /* -----------------------------------------------------------
     *  Unary helpers  (dereference, reference, -, ~, abs/floor,
     *                  int↔float conversions)
     * -------------------------------------------------------- */

    /* *expr  or  [] dereference handling */
    dereference = function (operator, expr, sourceCode, sourceOffset) {
        expr = metaSlot(expr);
        if (expr.operator === '+') {
            /*  &a + i   ->   PEEK (&a , i)  */
            expr.operator = '=[]';
        } else if (expr.operator === '-' && expr.operands[2] &&
                   expr.operands[2][0] === '#') {
            /*  &a - #n  where n is const -> adjust to negative literal */
            expr.operator = '=[]';
            var num = parseFloat(expr.operands[2].substr(1));   // strip leading '#'
            expr.operands[2] = '#'+(-num);
        } else {
            /* generic “*expr” */
            makeMeta(
                expr, operator, '?',
                undefined,
                makeRValue(expr),
                undefined
            );
        }
    };

    /* & (address-of) operator handling */
    reference = function (operator, expr, sourceCode, sourceOffset) {

        expr = metaSlot(expr);
        if (expr.oobIndex !== undefined && expr.oobIndex.own) {
            returnBack(expr.oobIndex.k);    /* the guard's copy dies with the finding - and so does
                                                        the `! MOVi` that made it, or an address would ship
                                                        a line nothing reads (flushMetaCode skips a null) */
            metacode[expr.oobIndex.copyAt].operator = null;
        }
        expr.oobIndex = undefined;                   /* address formation is never bounds-checked, at any
                                                        index - see checkConstIndex. Cleared before the
                                                        `=[]$` branch below calls makeRValue. */

        if (expr.operator === '=') {                 // variable
            assert(expr.operands[2] === undefined,
                   "expr.operands[2] must be void for '=' lvalue");
            expr.operator  = '=&';
            expr.operands[2] = '*0';

        } else if (expr.operator === '=*' || expr.operator === ':=*') {
            assert(expr.operands[2] === undefined,
                   "expr.operands[2] must be void for '=*' lvalue");
            expr.operator = ':=';                    // treat as plain r-value

        } else if (expr.operator === '=[]') {        // array element
            expr.operator = '+';                     // &a[i]  ->  &a + i

        } else if (expr.operator === '=[]$') {       // local array element
            expr.operator = '=&';
            var index = expr.operands[2];            // save index before clobber
            expr.operands[2] = '*0';
            makeMeta(
                expr, '+', 'p',
                undefined,
                makeRValue(expr),           // &base
                index                                // + offset
            );

        } else {
            fail('Invalid lvalue', sourceCode, sourceOffset, 'E404');
        }
    };

    /* unary minus (integer/float) */
    minus = function (operator, expr/*, src, off*/) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '-', undefined,
            undefined,
            ZEROES[ expr.type ],            // 0  of same type
            makeRValue(expr)
        );
    };

    /* bit-wise NOT / logical NOT  (~expr) */
    not = function (operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '^', undefined,
            undefined,
            makeRValue(expr),
            '#-1'                                    // XOR with -1
        );
    };

    /* ABS or FLOOR (unary) - operator is already '=abs' or '=floor' */
    absFloor = function (operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, operator, undefined,
            undefined,
            makeRValue(expr),
            undefined
        );
    };

    /* int -> float */
    intToFloatConvert = function (operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '=itof', undefined,
            undefined,
            makeRValue(expr),
            '#1.0'
        );
    };

    /* float -> int, with constant-fold special-case */
    floatToIntConvert = function (operator, expr) {

        expr = metaSlot(expr);

        var op1 = expr.operands[1],
            op2 = expr.operands[2];

        /* expr is  (const|<tmp>) * #1.0  pattern */
        if (expr.operator === '*' && op2 && span(op2[0], '#<') === 1) {
            makeMeta(expr, '=ftoi', undefined, undefined, op1, op2);

        } else if (expr.operator === '*' && op1 && span(op1[0], '#<') === 1) {
            makeMeta(expr, '=ftoi', undefined, undefined, op2, op1);

        } else {    // generic cast
            makeMeta(
                expr, '=ftoi', undefined,
                undefined,
                makeRValue(expr),
                '#1.0'
            );
        }
    };

    /* -----------------------------------------------------------
     *  UNARY_OPS dispatch table
     * -------------------------------------------------------- */

    UNARY_OPS = {};          /* will hold “=xxx” -> handler */

    /* no-op casts */
    function noop() {}

    /* register the handlers */
    map(UNARY_OPS,
        '=float',     noop,
        '=funcptr',   noop,
        '=int',       noop,
        '=pointer',   noop,

        '=*',         dereference,
        '=&',         reference,
        '=-',         minus,
        '=~',         not,
        '=abs',       absFloor,
        '=itof',      intToFloatConvert,
        '=ftoi',      floatToIntConvert,
        '=floor',     absFloor
    );

    /* -----------------------------------------------------------
     *  Generic unary operator
     * -------------------------------------------------------- */
    unaryOp = function (operator, expr, sourceCode, sourceOffset) {

        expr = metaSlot(expr);

        /* *structPointer -> a struct place (through-pointer field access / whole-struct assign) */
        if (operator === '*' && expr.type === 'p' && isStructAtom(expr.elem)) {
            setPlace(expr, 'pointer', makeRValue(expr), [], expr.elem, expr.elem);
            return;
        }

        /* &structValue -> a typed struct pointer (the place's address) */
        if (operator === '&' && expr.place) {
            var structName = expr.struct;
            if (expr.baseKind === 'local' && (!expr.offParts || expr.offParts.length === 0) && expr.dynIndex === undefined) {
                /* a whole local's address is a single ADRL with no offset scratch - leave it DEFERRED as
                   '=&' so an assignment emits ADRL straight into its target ($p) instead of a temp + MOVp,
                   exactly like &scalar / &array[i] defer in reference() */
                var sz = isStructAtom(structName) ? structAllocSize(structName) : '*0';
                makeMeta(expr, '=&', 'p', undefined, expr.base, sz);
            } else {                                          /* offset fold or global/pointer base: materialize now */
                makeMeta(expr, ':=', 'p', undefined, placeAddress(expr), undefined);
            }
            setElem(expr, structName);
            return;
        }

        var key      = '=' + operator;                // e.g. "=abs"
        var prevType = expr.type;                     /* for element-type propagation (Impala 2) */
        var prevElem = expr.elem;

        /* check type support */
        var sig  = key + expr.type;                   // e.g. "=absf"
        var rTyp = SUPPORTED_OPS[ sig ];
        if (rTyp == null) {
            typeError('Invalid type ({$type1})',
                               sourceCode, sourceOffset, expr.type, undefined, 'E302');
        }

        /* dispatch actual work */
        var fn = UNARY_OPS[ key ];
        if (fn) {
            fn(key, expr, sourceCode, sourceOffset);
        }

        /* update resulting type */
        expr.type = rTyp;

        /* element-type propagation (Impala 2) */
        if (key === '=*') {
            if (prevElem !== undefined) {
                expr.type = descHead(prevElem);          /* typed dereference: no cast needed */
                expr.elem = descTail(prevElem);
            } else {
                expr.elem = undefined;
            }
        } else if (key === '=&') {
            if (prevType === 'F' && isFuncTypeAtom(prevElem)) {
                expr.elem = prevElem;                             /* &funcptr -> element is the named funcptr type
                                                                     (its name already identifies it, like a struct) */
            } else {
                expr.elem = (prevType === undefined ? undefined   /* &x yields a pointer to x's type */
                        : prevType + (prevElem !== undefined ? ':' + prevElem : ''));
            }
        } else {
            expr.elem = undefined;                                /* casts and numeric ops erase */
        }

        /* a cast/deref/unary result is no longer a verbatim call result,
           so drop any callInfo that would let assign() pin the callee's
           return type to the l-value (e.g. *(pointer)f() must not make f
           "expect" the deref's target type). */
        expr.callInfo = undefined;
    };

    /* -----------------------------------------------------------
     *  Symbol declaration helper
     * -------------------------------------------------------- */

    function declare(kind, scope, name, type, readonly, value, sourceCode, sourceOffset, comment, elem) {
        /* emit data / flush pending code */
        if (kind !== undefined) {
            flushMetaCode('');

            if (typeof output === 'function') {
                var line = '';
                if (scope === 'locals') line += (typeof TAB !== 'undefined' ? TAB : '\t');
                if (name != null)       line += name + ':';
                line += '\t' +
                        replace(kind, '?', TYPE_SUFFIXES[type] || '');
                if (value !== undefined) line += ' ' + value;
                if (comment)             line += '\t; ' + comment;

                output( line );
            }
        }

        /* give any temporary back to the pool */
        returnBack(value);

        /* register in symbol table */
        if (name !== undefined) {
            claimTopName(name, TOP_KINDS[scope], sourceCode, sourceOffset);
            var table = symbols[scope];
            var prev  = table && table[name];

            if (prev) {
                if (kind !== undefined && prev.kind !== undefined) {
                    /* a local is TABLED as `$b`; the user wrote `b`, so report what they wrote */
                    fail('Identifier already declared: '
                                          + (name.charAt(0) === '$' ? name.substr(1) : name),
                                  sourceCode, sourceOffset, 'E401');
                }
                if (type !== prev.type) {
                    typeError('Type mismatch with previous declaration of ' +
                                       name + ' (was {$type1})',
                                       sourceCode, sourceOffset, prev.type, undefined, 'E402');
                }
                if (prev.elem !== undefined && elem !== undefined && prev.elem !== elem) {
                    fail('Element type mismatch with previous declaration of ' + name
                                  + ' (was ' + elemVerbose(prev.elem) + ', now '
                                  + elemVerbose(elem) + ')',
                                  sourceCode, sourceOffset, 'E203');
                }
                /* inherit old flags */
                kind     = (kind     !== undefined ? kind     : prev.kind);
                readonly = (readonly || prev.readonly);
                elem     = (elem     !== undefined ? elem     : prev.elem);
            }

            /* store / update */
            if (!table) symbols[scope] = table = {};
            table[name] = {
                type: type,
                elem: elem,
                readonly: !!readonly,
                kind: kind,
                signature: prev && prev.signature,
                externProto: prev && prev.externProto,   /* survives the definition's signature reset */
                extent: prev && prev.extent,             /* ditto: this REBUILDS the record, so an array's
                                                            extent must be carried or a later `extern array g`
                                                            - the ordinary import-closure shape - drops it and
                                                            silently disarms E461 and --range-checks */
                sourceCode: sourceCode,
                sourceOffset: sourceOffset,
                sourceName: (sourceName !== undefined ? sourceName
                                                               : (prev ? prev.sourceName : undefined))
            };
        }
    };

    /* -----------------------------------------------------------
     *  Flush all queued meta-code into final text output
     * -------------------------------------------------------- */
    flushMetaCode = function (prefix) {

        prefix = prefix || '';
        var TABstr = (typeof TAB !== 'undefined') ? TAB : '\t';

        var nextLabel   = TABstr;   // pending label prefix
        var nextRide    = false;    // ...and whether it may ride a `!` line instead of costing a NOOP
        var nextComment = '';       // pending trailing comment
        function formatOperand(op) {
            return (op == null ? '' : op);
        }

        for (var i = 0; i < metacode.length; ++i) {

            var rec   = metacode[i];
            var op    = rec.operator;

            if (op == null) {
                /* empty / removed meta - skip */
                continue;
            }

            /* -------------------------------------------------- */
            /* handle a stand-alone label (“<--”)                 */
            /* -------------------------------------------------- */
            if (op === '<--') {
                assert(rec.operands[0][0] === '@',
                       "label must start with '@'");
                if (nextLabel !== TABstr) {
                    output(prefix + nextLabel + 'NOOP');
                }
                nextLabel = rec.operands[0].substr(1) + ':' + TABstr;
                nextRide  = (rec.mayRide === true);
                continue;
            }

            /* -------------------------------------------------- */
            /* comment pseudo-op (“; ...”)                          */
            /* -------------------------------------------------- */
            if (op === ';') {
                nextComment = '\t; ' +
                             replace(rec.operands[0], '\t', ' ');
                continue;
            }

            /* -------------------------------------------------- */
            /* compile-time op (string starts with '<> ')         */
            /* -------------------------------------------------- */
            if (op.substr(0, 3) === '<> ') {

                /* A label RIDES this `!` line only if its minter said so. The assembler resolves
                   `! LSSi .. @L` against a line that folds away, but a runtime `GOTO @L` then reports
                   "Symbol not found (in expected scope)" - and a label nobody operands at all (a switch
                   case, which SWCH spells out of a table base) would vanish entirely. Only the minter
                   knows which it is, so it flags the record rather than have this pass guess. */
                if (nextLabel !== TABstr && !nextRide) {
                    output(prefix + nextLabel + 'NOOP');
                    nextLabel = TABstr;
                }

                var gop = META_TO_GAZL[ op.substr(3) ];
                gop      = replace(gop, '?',
                                   TYPE_SUFFIXES[ rec.type ]);

                output(prefix + nextLabel + '! ' + gop + ' ' +
                       formatOperand(rec.operands[0]) + ' '   +
                       formatOperand(rec.operands[1]) + ' '   +
                       formatOperand(rec.operands[2]) + nextComment);

                nextLabel   = TABstr;
                nextComment = '';
                continue;
            }

            /* -------------------------------------------------- */
            /* normal run-time instruction                        */
            /* -------------------------------------------------- */
            var gInstr = META_TO_GAZL[ op ];
            gInstr     = replace(gInstr, '?',
                                 TYPE_SUFFIXES[ rec.type ]);

            output(prefix + nextLabel + gInstr + ' ' +
                   formatOperand(rec.operands[0]) + ' ' +
                   formatOperand(rec.operands[1]) + ' ' +
                   formatOperand(rec.operands[2]) + nextComment);

            nextLabel   = TABstr;
            nextComment = '';
        }

        /* reset queue */
        metacode.length = 0;
    };

    /* -----------------------------------------------------------
     *  Identifier lookup helper
     * -------------------------------------------------------- */
    lookup = function (x, name, isGlobal, sourceCode, sourceOffset) {

        var sym = symbols;
        var p   = null;

        /* local  ------------------------------------------------*/
        if (!isGlobal && (p = sym.locals['$' + name])) {

            if (p.type === 'S') {                                 /* struct value local -> a place */
                setPlace(x, 'local', '$' + name, [], p.elem, p.elem);
                return;
            }
            if (p.type === 'A' && isStructAtom(p.elem)) {   /* struct-element array -> a foldable local array place (base:offset, no ADRL) */
                setPlace(x, 'local', '$' + name, [], undefined, p.elem, p.elem);
                x.extent = p.extent;                                  /* after setPlace, which clears it */
                return;
            }
            if (p.type === 'A') {
                makeMeta(x, '=&', 'p', undefined,
                                  '$' + name, '*0');
                x.extent = p.extent;                                  /* after makeMeta, which clears it */
            } else {
                makeMeta(x,
                                  (p.readonly ? ':=' : '='),
                                  p.type,
                                  undefined,
                                  '$' + name,
                                  undefined);
            }
            setElem(x, p.elem);
            return;
        }

        /* global -----------------------------------------------*/
        if (isGlobal && (p = sym.globals[name])) {

            if (p.type === 'S') {                                 /* struct value global -> a place in global memory */
                setPlace(x, 'globalAddr', '&' + name, [], p.elem, p.elem);
                metaSlot(x).readonly = (p.readonly === true);      /* a field write must see it too - CNST
                                                                     rejects the POKE only at load, if at all */
                return;
            }
            if (p.type === 'A') {
                makeMeta(x, ':=', 'p', undefined,
                                  '&' + name, undefined);
                x.extent = p.extent;                                  /* after makeMeta, which clears it */
                /* A scalar `readonly` is rejected by its `:=*` operator having no assign() branch, but an
                   ARRAY decays to the same `:=` address meta whether or not it is writable - so carry the
                   flag and let assign() consult it once the subscript has been folded in. */
                x.readonly = (p.readonly === true);
            } else {
                makeMeta(x,
                                  (p.readonly ? ':=*' : '=*'),
                                  p.type,
                                  undefined,
                                  '&' + name,
                                  undefined);
                x.readonly = (p.readonly === true);               /* makeMeta clears it; assign() reads
                                                                     the FLAG, not the operator spelling */
            }
            setElem(x, p.elem);
            return;
        }

        /* `global` names a storage table, and neither a function nor a const is in one - so the two
           branches below used to ignore the flag entirely and the keyword was silently discarded. */
        if (isGlobal && (sym.functions[name] || sym.defines[name])) {
            strictError('`global` is only for global variables - ' + name + ' is a '
                            + (sym.functions[name] ? 'function' : 'constant'),
                    sourceCode, sourceOffset, 'E452', 'drop the `global` keyword');
        }

        /* function ---------------------------------------------*/
        if ((p = sym.functions[name])) {
            if (p.type === 'N') {
                makeMeta(x, ':=', 'N', undefined,
                                  '^' + name, undefined);
            } else {
                assert(p.type === 'U', 'function entry must be U');
                makeMeta(x, ':=', 'F', undefined,
                                  '&' + name, undefined);
            }
            setElem(x, undefined);
            return;
        }

        /* constant / #define -----------------------------------*/
        if ((p = sym.defines[name])) {
            makeMeta(x, ':=', p.type, undefined,
                              '#' + name, undefined);
            setElem(x, p.elem);
            return;
        }

        /* not found --------------------------------------------*/
        /* Before giving up, look in the OTHER namespace. `global` is a mandatory prefix at every use
           site, so reading a global without it - or a local/const with it - is the first error most
           newcomers and code generators hit, and a bare "Undeclared identifier" points away from the
           one-word fix. The symbol tables already hold the answer; say it. */
        var hint;
        if (!isGlobal && sym.globals[name]) {
            hint = name + ' is a global - write `global ' + name + '`';
        } else if (isGlobal && sym.locals['$' + name]) {
            hint = name + ' is a local - drop the `global` keyword';
        }
        fail('Undeclared identifier: ' + name,
                      sourceCode, sourceOffset, 'E403', hint);
    };

    /* -----------------------------------------------------------
     *  Ensure expression resolves to a compile-time constant
     * -------------------------------------------------------- */
    makeConstant = function (x, wantType,
                                      sourceCode, sourceOffset) {

        var r = makeRValue(x, '#<&');

        if (x.type !== wantType ||
            span(r[0], '#<&') !== 1) {

            fail(
                bake('Expected constant ' +
                     VERBOSE_TYPES[ wantType ]),
                sourceCode, sourceOffset, 'E407');
        }
        return r;
    };

    /* -----------------------------------------------------------
     *  Constant subtraction helper
     * -------------------------------------------------------- */
    subConstInt = function (opL, opR) {

        assert(span(opR[0], '#<') === 1,
               "rhs must be const");

        /* trivial cases */
        if (opR === '#0') return opL;

        if (opL[0] === '#' && opR[0] === '#' &&
            span(opL.substr(1), '0123456789') === opL.length - 1 &&
            span(opR.substr(1), '0123456789') === opR.length - 1) {

            return '#' + ( parseInt(opL.substr(1), 10) -
                           parseInt(opR.substr(1), 10) );
        }

        /* need run-time temp */
        returnBack(opL);

        var tmp;
        if (span(opL[0], '#<') === 1) {
            emit('<> -', 'i',
                          tmp = borrow('<'),
                          opL, opR);
        } else {
            emit('-', 'i',
                          tmp = borrow('%'),
                          opL, opR);
        }
        return tmp;
    };

    /* drop leading “#” helper */
    dropHash = function (s) {
        return (s[0] === '#') ? s.substr(1) : s;
    };

    /* printable ASCII table (33-126) */
    printable = '';
    for (var i = 33; i < 127; ++i) {
        printable += char(i);
    }

    /* -----------------------------------------------------------
     *  Dump a string constant into assembly
     * -------------------------------------------------------- */
    dumpString = function (label, str) {

        var len = str.length;
        declare('CNST', 'globals', label, '?',
                         true, '*' + len, '', 0);

        var offset = 0;
        while (offset < len) {

            /* raw bytes before printable chunk */
            var non = find(str.substr(offset), printable);
            if (non > 0) {
                var d = '';
                for (var k = 0; k < non; ++k) {
                    d += ' #' + ordinal(str[offset + k]);
                }
                declare('DATi', undefined, undefined, 'i',
                                 true, d.substr(1), '', 0);
                offset += non;
                continue;
            }

            /* printable (plus spaces) */
            var spanLen = span(str.substr(offset),
                               printable + ' ');
            spanLen = rspan(str.substr(offset, spanLen), ' ');
            if (spanLen > 0) {
                declare('DATs', undefined, undefined, 'i',
                                 true, str.substr(offset, spanLen), '', 0);
                offset += spanLen;
            }
        }
    };

    /* -----------------------------------------------------------
     *  Manage / share string literals
     * -------------------------------------------------------- */
    makeString = function (prefix, x, s,
                                    sourceCode, sourceOffset) {

        s += char(0);       // NUL-terminate

        var byteString = '';
        for (var idx = 0; idx < s.length; ++idx) {
            byteString += char(ordinal(s[idx]));
        }
        s = byteString;

        var tbl = strings[prefix];
        if (!tbl) {
            tbl = strings[prefix] = [];
            tbl.rlookup = {};
        }

        var entry = tbl.rlookup[s];
        if (entry == null) {

            /* generate unique label */
            var name = '.' + prefix + '_' +
                       (s.replace(/[^0-9a-zA-Z]/g, '')
                          .substr(0, 6)) +
                       '_' +
                       ( ((randomId + tbl.length) >>> 0)
                         .toString(16) );

            if (noForward) {
                dumpString(name, s);
            } else {
                tbl.push({ name:name, data:s });
            }

            /* add all suffixes to rlookup */
            for (var k = 0; k < s.length; ++k) {
                tbl.rlookup[ s.substr(k) ] =
                    name + (k ? ':' + k : '');
            }
            entry = name;            // full string label
        }

        makeMeta(x, ':=', 'p',
                          undefined, '&' + entry, undefined);
    };

    /* -----------------------------------------------------------
     *  Tiny utilities still missing from the toolbox
     * -------------------------------------------------------- */

    /* wipe an Array or plain Object in-place */
    function prune(o) {
        if (!o) return;
        if (o instanceof Array) {
            o.length = 0;
        } else {
            for (var k in o) {
                delete o[k];
            }
        }
    }

    /* simple “foreach” - fn(element, index) */
    function iterate(arr, fn) {
        for (var i = 0; i < arr.length; ++i) {
            fn(arr[i], i);
        }
    }

    /* -----------------------------------------------------------
     *  Compiler start / end hooks
     * -------------------------------------------------------- */

    start = function () {

        /* reset per-compilation state */
        if (!stock) stock = { '%': [], '<': [] };
        if (!counters) counters = { '%': 0, '<': 0 };
        if (!metacode) metacode = [];
        if (!symbols) symbols = {};
        if (!strings) strings = { s: [], a: [] };
        if (!switchStack) switchStack = [];

        /* clear transient pools and counters */
        var poolPercent = stock['%'] || (stock['%'] = []);
        var poolAngle   = stock['<'] || (stock['<'] = []);
        resetQueue(poolPercent);
        resetQueue(poolAngle);
        counters['%'] = 0;
        counters['<'] = 0;

        /* wipe accumulated meta-instructions */
        metacode.length = 0;
        labelCounter    = 0;
        switchStack.length = 0;

        /* reset symbol tables */
        symbols.locals   = {};
        symbols.globals  = {};
        symbols.functions = {};
        symbols.defines  = {};
        structs          = {};
        functypes        = {};
        topNames         = {};
        guardCounter     = 0;
        exportNext       = false;

        /* reset deferred string tables */
        strings.s = [];
        strings.s.rlookup = {};
        strings.a = [];
        strings.a.rlookup = {};

        noForward = false;

        /* random-id seeding */
        if (typeof hostRandomId !== 'undefined') {
            randomId = hostRandomId;
        } else {
            for (var i = 0; i < 1000; ++i) {
                randomId =
                    floor(random() * 0xFFFFFFFF) ^ time();
            }
        }

        /* banner */
        var LF = '\n';
        output('; Compiled with Impala version ' +
               IMPALA_VERSION + LF);
        output('; signatures version=1');
    };

    end = function () {

        /* dump deferred string literals */
        iterate(strings.s, function (rec) {
            dumpString(rec.name, rec.data);
        });

        /* dump assert strings only if present */
        if (strings.a.length > 0) {

            output('\t! EQUi #DEBUG #0 @.noAssertStrings');

            iterate(strings.a, function (rec) {
                dumpString(rec.name, rec.data);
            });

            output('.noAssertStrings:\t!');
        }
    };
};function root($){return (function(){var _b=_i;return _($)&&(function(){ start(); ; return true})()&&((function(){while((function(){var _b=_i;return (function(){ declOffset = _i; declSource = _s; ; return true})()&&(function(){var _b=_i;return ImportDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ExportDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FuncDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||StructDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FuncTypeDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ExternDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ConstDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GlobalDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _l=_i,_x=(!!_s[_i])&&(++_i,true);_i=_l;return !_x})()&&(function(){ end(); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ImportDecl($){var $path=createParserContext();return (function(){var _b=_i;return IMPORT($)&&_($)&&StringLiteral($path)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ExportDecl($){return (function(){var _b=_i;return EXPORT($)&&_($)&&(function(){ exportNext = true; ; return true})()&&(function(){var _b=_i;return FuncDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GlobalDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ConstDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ exportNext = false; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function StructDecl($){var $id=createParserContext(),$sname,$f=createParserContext();return (function(){var _b=_i;return STRUCT($)&&_($)&&Identifier($id)&&(function(){ $sname = $id._; beginStruct($id._, _s, _i); ; return true})()&&(_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return ArrayDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ addStructField($sname, { name: $f.name, type: $f.type, elem: $f.elem, struct: $f.struct, size: $f.size }, _s, _i); ; return true})()&&((function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){   endStruct($sname); /* Publish the real layout with TYPES so gazl-validate can check an `extern struct` declaration of the same name against it - the .o./.z. constants alone carry no types. */ emitStandaloneSignatureComment( structSignatureRow($sname, false, sourceName, _s, declOffset)); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncTypeDecl($){var $id=createParserContext(),$ftname,$p=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return FUNCTYPE($)&&_($)&&Identifier($id)&&(function(){ $ftname = $id._; beginFuncType($id._, _s, _i); ; return true})()&&(_s[_i]==="(")&&(++_i,true)&&_($)&&((function(){var _b=_i;return TypeDeclr($p)&&(function(){ addFuncTypeParam($ftname, $p.type, $p.elem, $p.struct, $p.words, $p.name, _s, _i); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&TypeDeclr($p)&&(function(){ addFuncTypeParam($ftname, $p.type, $p.elem, $p.struct, $p.words, $p.name, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&((function(){var _b=_i;return RETURNS($)&&_($)&&TypeDeclr($r)&&(function(){ addFuncTypeReturn($ftname, $r.type, $r.elem, $r.struct, $r.words, _s, _i); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&TypeDeclr($r)&&(function(){   /* PARKED for Impala 3.0 - see docs/ParkedFeatures.md */ fail('Multiple return values are not supported in Impala 2.0', _s, _i, 'E428', 'declare a single return type for this funcptr type'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ endFuncType($ftname); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TypeDeclr($){var $base=createParserContext(),$desc,$id=createParserContext();return (function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&((function(){var _b=_i;return Identifier($id)&&(function(){ $.name = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ var head = descHead($desc); var tail = descTail($desc); if (tail === undefined && isStructAtom(head)) { $.type = 'S'; $.struct = head; $.elem = undefined; $.words = structWords(head); } else if (tail === undefined && isFuncTypeAtom(head)) { $.type = 'F'; $.elem = head; $.struct = undefined; $.words = 1; } else { $.type = head; $.elem = tail; $.struct = undefined; $.words = undefined; } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncDecl($){var $inl,$inlAt,$id=createParserContext(),$inp=createParserContext(),$out=createParserContext(),$v=createParserContext(),$,$loc=createParserContext();return (function(){var _b=_i;return (function(){ $inl = false; ; return true})()&&((function(){var _b=_i;return INLINE($)&&(function(){ $inl = true; $inlAt = _i; ; return true})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&FUNCTION($)&&_($)&&Identifier($id)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ if ($inl) {   /* PARKED for Impala 3.0 - see docs/ParkedFeatures.md */ fail('Inline functions are not supported in Impala 2.0', _s, $inlAt, 'E439', 'an expansion needs GAZL 2 `SCOP` / `ENDS` to place its locals; ' + 'drop `inline` to compile it as an ordinary function'); } assert(validateStock('%')); assert(validateStock('<')); /* every compile-time scratch borrowed by the previous function/globals must be back in the pool at this clean boundary - catches offset-scratch leaks at the source */ assert(stock['<'].length === counters['<'], 'compile-time scratch leak before ' + $id._ + ': ' + (counters['<'] - stock['<'].length) + ' unreturned'); output(''); output(';-----------------------------------------------------------------------------'); /* declare the function symbol */ declare( undefined, 'functions', $id._, 'U', true, undefined, _s, _i ); var entry = symbols.functions[$id._]; if (entry) { if (!entry.signature) { entry.signature = {}; } entry.signature.params = []; entry.signature.returns = '?'; entry.signature.returnElem = undefined; entry.signature.returnName = undefined; entry.signature.sourceCode = _s; entry.signature.sourceOffset = declOffset; entry.signature.sourceName = sourceName; entry.signature.returnResolved = false; entry.pendingReturnPlaceholder = undefined; entry.pendingReturnDeclaration = undefined; } ; return true})()&&ArgsDecl($inp)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){var _b=_i;return RETURNS($)&&_($)&&VarDecl($out)&&(function(){ var entry = symbols.functions[$id._]; if (entry) { entry.pendingReturns = undefined; addReturn(entry, $out.name, $out.type, $out.elem, $out.size, _s, _i, $out.struct, $out.words); } ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($v)&&(function(){   /* PARKED for Impala 3.0 - see docs/ParkedFeatures.md */ fail('Multiple return values are not supported in Impala 2.0', _s, _i, 'E428', 'return one value, or pass extra results back through pointer out-parameters'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ var entry = symbols.functions[$id._]; if (entry && entry.signature) { var rl = entry.pendingReturns; entry.signature.returnList = rl; entry.signature.returnCount = rl.length; entry.signature.returns = rl[0].type; entry.signature.returnElem = rl[0].elem; entry.signature.returnName = rl[0].rawName; entry.signature.returnStruct = rl[0].struct; var _rw = 0;                    /* total output-window words (struct returns span >1) */ for (var _wi = 0; _wi < rl.length; ++_wi) _rw += (rl[_wi].type === 'S' ? rl[_wi].words : 1); entry.signature.returnWords = _rw; resolveFunctionReturnType($id._, rl[0].type, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ /* implicit 1-word return: even void functions expose a single-word PARA so legacy call sites keep a deterministic return slot and the JSPEG output matches the historical PPEG layout. */ var entry = symbols.functions[$id._]; if (entry) { entry.pendingReturns = undefined; entry.pendingReturnPlaceholder = { sourceCode: _s, sourceOffset: _i }; } if (entry && entry.signature) { entry.signature.returns = 'V'; entry.signature.returnElem = undefined; entry.signature.returnName = undefined; entry.signature.returnCount = 0; entry.signature.returnList = []; resolveFunctionReturnType($id._, 'V', _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ /* declare input parameters */ var entry = symbols.functions[$id._]; if (entry && entry.signature) { entry.signature.params = []; for (var idx = 0; idx < $inp.n; ++idx) { var param = $inp._[idx]; entry.signature.params.push({ type: param.type, elem: param.elem, name: param.name, size: param.size, struct: param.struct, words: param.words }); } } emitFunctionSignature($id._, _s, _i); if (entry) { if (entry.pendingReturns && entry.pendingReturns.length > 0) { for (var _ri = 0; _ri < entry.pendingReturns.length; ++_ri) { var ret = entry.pendingReturns[_ri]; rejectByValueStruct(ret.type, ret.struct, ret.rawName, true, ret.sourceCode, ret.sourceOffset); { declare( 'OUT?', 'locals', ret.name, ret.type, false, ret.size, ret.sourceCode, ret.sourceOffset, undefined, ret.elem ); } } entry.pendingReturns = undefined; } else if (entry.pendingReturnPlaceholder) { var placeholder = entry.pendingReturnPlaceholder; declare( 'PARA', 'locals', undefined, '?', false, '*1', placeholder.sourceCode, placeholder.sourceOffset ); entry.pendingReturnPlaceholder = undefined; } } iterate($inp._, function (p) { rejectByValueStruct(p.type, p.struct, p.name, false, _s, _i); { declare( 'INP?', 'locals', '$' + p.name, p.type, true, (p.size !== undefined ? '*' + p.size : undefined), _s, _i, undefined, p.elem ); } }); ; return true})()&&((function(){var _b=_i;return LOCALS($)&&_($)&&LocalsDecl($loc)&&(function(){ iterate($loc._, function (v) { if (v.type === 'S') {         /* struct value local -> LOCA *sizeof, remember struct */ declare( 'LOCA', 'locals', '$' + v.name, 'S', false, structAllocSize(v.struct), _s, _i, undefined, v.struct ); } else { declare( 'LOC?', 'locals', '$' + v.name, v.type, false, (v.type === 'A' ? arrayAllocSize(v.elem, v.size, extentSymbol(v.name, $id._)) : (v.words !== undefined ? '*' + v.words : undefined)), _s, _i, undefined, v.elem ); } if (v.type === 'A') {   /* the ONLY place the owning function's name is in scope, so what a later subscript needs to bounds-check this array is recorded here - see arrayExtent's shape */ symbols.locals['$' + v.name].extent = { n: v.size, what: v.name, sym: extentSymbol(v.name, $id._) }; } }); iterate($loc._, function (v) {   /* the count scratches ArrayDecl held for this clause (see there); a no-op for the ones a declaration above already gave back */ returnBack(v.size); }); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ output(';-----------------------------------------------------------------------------'); ; return true})()&&Block($)&&(function(){ /* wrap-up body */ /* The closing RETU goes in BEFORE the pass, so the pass can see it: `goto out;` where `out:` is the end-of-body label is Impala's only early-exit idiom, and it lands exactly here. */ emit('--^', undefined, undefined, undefined, undefined); processBranches(); flushMetaCode('\t'); emittedGuards = {};   /* per function: labels are too */ prune(symbols.locals); labelCounter = 0; output(''); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ExternDecl($){var $at,$id=createParserContext(),$sname,$f=createParserContext(),$inp=createParserContext(),$out=createParserContext(),$desc,$type=createParserContext();return (function(){var _b=_i;return EXTERN($)&&_($)&&(function(){ $.scope = 'globals'; $.structFwd = false; $at = _i;   /* the declaration itself - end-of-rule positions have skipped past it */ pendingProto = undefined; ; return true})()&&(function(){var _b=_i;return STRUCT($)&&_($)&&Identifier($id)&&(function(){ $.structFwd = true; $sname = $id._; beginExternStruct($id._, _s, _i); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ /* a bodyless `extern struct G` never reaches endStruct, so only the braced form opens */ openStruct = $sname; ; return true})()&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return ArrayDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ addStructField($sname, { name: $f.name, type: $f.type, elem: $f.elem, struct: $f.struct, size: $f.size }, _s, _i); ; return true})()&&((function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){   /* Declare the host-owned interface so gazl-validate can check the layout the host supplies (.o.Name.field / .z.Name) against what Impala assumed, the way extern globals and function signatures are already checked. A re-declaration of a struct DEFINED here describes no host layout, so it publishes no row - the definition already published the real one. */ if (!endStruct($sname)) { emitStandaloneSignatureComment( structSignatureRow( $sname, true, sourceName, _s, declOffset)); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){var _b=_i;return FUNCTION($)&&(function(){ $.type  = 'U';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NATIVE($)&&(function(){ $.type  = 'N';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&Identifier($id)&&(function(){ $.name  = $id._; ; return true})()&&((function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&ArgsDecl($inp)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){   /* Optional PROTOTYPE. Name-only stays valid and asserts nothing (a wildcard the validator skips); a prototype is a checkable assertion, so calls get argument type-checking and the emitted signature row carries real types. */ var _pa = []; for (var _pk = 0; _pk < $inp.n; ++_pk) { var _pv = $inp._[_pk]; _pa.push({ type: _pv.type, elem: _pv.elem, name: _pv.name, size: _pv.size, struct: _pv.struct, words: _pv.words }); } pendingProto = { args: _pa, ret: undefined }; ; return true})()&&((function(){var _b=_i;return RETURNS($)&&_($)&&VarDecl($out)&&(function(){ if (pendingProto) pendingProto.ret = { type: $out.type, elem: $out.elem, struct: $out.struct, name: $out.name }; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($)&&(function(){   /* PARKED for Impala 3.0 - see docs/ParkedFeatures.md */ fail('Multiple return values are not supported in Impala 2.0', _s, _i, 'E428', 'declare a single return value for this extern'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ $desc = undefined; ; return true})()&&((function(){var _b=_i;return BASE_TYPE($type)&&_($)&&(function(){ $desc = CASTS_TO_TYPES[$type._]; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&ARRAY($)&&_($)&&Identifier($id)&&(function(){ $.type = 'A'; $.name = $id._; $.elem = $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ if ($.structFwd) { cancelStructRedeclaration($sname); return true; } declare( undefined,                 // no section for extern
                                                                             $.scope, $.name, $.type, false,                     // not readonly
                                                                             '?', _s, _i, undefined, $.elem ); if ($.scope === 'functions') { var entry = symbols.functions[$.name]; var signature = entry && entry.signature; if (entry) { if (!signature) { signature = entry.signature = {}; } if (signature.sourceName === undefined) { signature.sourceName = sourceName; } if (signature.sourceCode === undefined) { signature.sourceCode = _s; signature.sourceOffset = declOffset; signature.sourceName = sourceName; } if (entry.kind !== 'FUNC') {  /* a definition here already resolved it - do not un-resolve it */ signature.returnResolved = false; } } var role = ($.type === 'N' ? 'extern native' : 'extern func'); var placeholderSignature = { params: [], returns: undefined, sourceName: sourceName, sourceCode: _s, sourceOffset: declOffset, }; var _proto = pendingProto; pendingProto = undefined; if (_proto !== undefined) {   /* a declared prototype: real params + at most one return */ var _pp = []; for (var _pi = 0; _pi < _proto.args.length; ++_pi) { var _p = _proto.args[_pi]; rejectByValueStruct(_p.type, _p.struct, _p.name, false, _s, _i); _pp.push({ type: _p.type, elem: _p.elem, name: _p.name, size: _p.size, struct: _p.struct, words: _p.words }); } var _pr = _proto.ret; if (_pr !== undefined) rejectByValueStruct(_pr.type, _pr.struct, _pr.name, true, _s, _i); placeholderSignature.params      = _pp; placeholderSignature.returns     = (_pr !== undefined ? _pr.type : 'V'); placeholderSignature.returnElem  = (_pr !== undefined ? _pr.elem : undefined); placeholderSignature.returnCount = (_pr !== undefined ? 1 : 0); placeholderSignature.returnWords = (_pr !== undefined ? 1 : 0); placeholderSignature.returnResolved = true; if (entry) { var _defined = (entry.kind === 'FUNC'); checkExternAgreement($.name, placeholderSignature, (_defined ? entry.signature : entry.externProto), _defined, _s, $at); entry.externProto = placeholderSignature; if (!_defined) {               /* a definition here outranks it; otherwise publish it so call sites check against it */ entry.signature.params         = _pp; entry.signature.returns        = placeholderSignature.returns; entry.signature.returnElem     = placeholderSignature.returnElem; entry.signature.returnCount    = placeholderSignature.returnCount; entry.signature.returnWords    = placeholderSignature.returnWords; entry.signature.returnResolved = true; } } } emitStandaloneSignatureComment( formatFunctionSignatureComment( $.name, placeholderSignature, role, sourceName, _s, declOffset ) ); } else if ($.scope === 'globals') { emitStandaloneSignatureComment( formatGlobalSignatureComment( 'GLOB', $.name, $.type, $.size, 'extern', sourceName, _s, declOffset, $.elem ) ); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ConstDecl($){var $base=createParserContext(),$desc,$nf,$t,$telem,$cStart,$id=createParserContext(),$cInitStart,$x=createParserContext();return (function(){var _b=_i;return CONST($)&&_($)&&TypeBase($base)&&(function(){ /* Same type grammar as every other declarator (TypeBase, not bare BASE_TYPE), so a const can name a struct pointer or a named functype - a const is an assembler-level address/scalar constant, and those two are just addresses. A struct VALUE is the one shape that has no scalar constant form. */ $desc = $base._; $nf = noForward; noForward = true; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ var chead = descHead($desc); var ctail = descTail($desc); if (ctail === undefined && isStructAtom(chead)) { fail('A const cannot be a struct value - use a struct pointer (' + 'const ' + chead + ' pointer)', _s, _i, 'E447'); } else if (ctail === undefined && isFuncTypeAtom(chead)) { $t = 'F'; $telem = chead;      /* named funcptr type constant */ } else { $t = chead; $telem = ctail; } $cStart = _i;   /* `Identifier` eats trailing space, so _i would name the NEXT declaration (E453) */ ; return true})()&&Identifier($id)&&(function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){ $cInitStart = _i; ; return true})()&&Expr($x)&&(function(){ declare( '! DEF?', 'defines', $id._, $t, true, makeConstant($x._, $t, _s, $cInitStart), _s, _i, formatConstSignatureComment( $id._, $t, sourceName, _s, declOffset, $telem ), $telem ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ /* `export` says "this unit provides it"; a valueless const says "someone else does". The row below publishes it as an extern either way, so the keyword was silently dropped. */ if (exportNext) { fail('`export` contradicts a valueless `const` - ' + $id._ + ' is provided elsewhere, not by this unit', _s, $cStart, 'E453', 'give it a value to export it, or drop `export`'); } declare( undefined, 'defines', $id._, $t, true, undefined, _s, _i, undefined, $telem ); emitStandaloneSignatureComment(  /* valueless -> host/runtime defines it: publish it as an extern so it links-checks */ formatConstSignatureComment( $id._, $t, sourceName, _s, declOffset, $telem, true ) ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ noForward = $nf; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GlobalDecl($){var $section,$v=createParserContext(),$vStruct,$init,$initStart,$d=createParserContext(),$binit,$x=createParserContext(),$a=createParserContext(),$aStructEl,$aStruct,$aCount;return (function(){var _b=_i;return (function(){var _b=_i;return GLOBAL($)&&(function(){ $section = 'GLOB'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||READONLY($)&&(function(){ $section = 'CNST'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||TEMPORARY($)&&(function(){ $section = 'TEMP'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&(function(){var _b=_i;return VarDecl($v)&&(function(){ $vStruct = ($v.type === 'S'); if ($vStruct) {              /* struct value global -> one zeroed GLOB/CNST/TEMP *sizeof */ declare( $section, 'globals', $v.name, 'S', ($section === 'CNST'), structAllocSize($v.struct), _s, _i, formatGlobalSignatureComment( $section, $v.name, 'S', undefined, undefined, sourceName, _s, declOffset, $v.struct), $v.struct ); } else { declare( $section, 'globals', undefined, $v.type, ($section === 'CNST'), '*1', _s, _i ); $init = ZEROES[$v.type]; } ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){ $initStart = _i; ; return true})()&&(function(){var _b=_i;return Braced($d)&&(function(){ if (!$vStruct) fail('Brace initializers are only for struct values', _s, $initStart, 'E422'); $binit = []; buildStructInit($v.struct, $d._, $binit, _s, $initStart); emitInitData($binit, _s, $initStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($x)&&(function(){ if ($vStruct)                    /* $initStart, not _i: `Expr` ate the trailing space too */ fail('A struct value needs a brace initializer', _s, $initStart, 'E421'); $init = makeConstant($x._, $v.type, _s, $initStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ if (!$vStruct) declare( 'DAT?', 'globals', $v.name, $v.type, ($section === 'CNST'), $init, _s, _i, formatGlobalSignatureComment( $section, $v.name, $v.type, undefined, undefined, sourceName, _s, declOffset, $v.elem ), $v.elem ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($a)&&(function(){ declare( $section, 'globals', $a.name, 'A', ($section === 'CNST'), arrayAllocSize($a.elem, $a.size, extentSymbol($a.name)), _s, _i, formatGlobalSignatureComment( $section, $a.name, 'A', $a.size, undefined, sourceName, _s, declOffset, $a.elem ), $a.elem ); symbols.globals[$a.name].extent = { n: $a.size, what: $a.name, sym: extentSymbol($a.name) }; returnBack($a.size);   /* declared: this consumer is done with it */ $aStructEl = ($a.elem !== undefined && descTail($a.elem) === undefined && isStructAtom(descHead($a.elem))); $aStruct = $a.elem; $aCount = parseInt('' + $a.size, 10); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){   /* the list is fully consumed before these checks run, so _i would land on the NEXT declaration - name the initializer itself */ $initStart = _i; /* Hand InitList the DECLARED element type. Without it every entry was checked against its own type, which no value can fail, so `int array A[2] = { 1, "s" }` stored a pointer in an int slot and `float array F[2] = { 1, 2 }` stored the INTEGER bit pattern and read back 1.4013e-45. The scalar paths have always been this strict (`global float f = 1` is E407); only the array path was not. Restricted to the scalar heads on purpose: a struct-element array must keep reporting the friendlier E422 below, and a funcptr element head is a TYPE NAME that makeConstant would not recognise. */ var _eh = ($a.elem !== undefined ? descHead($a.elem) : undefined); initElemType = (!$aStructEl && (_eh === 'i' || _eh === 'f' || _eh === 'p')) ? _eh : undefined; ; return true})()&&(function(){var _b=_i;return InitList($d)&&(function(){   /* flat list -> scalar-element arrays only */ if ($aStructEl) fail('A struct-element array needs nested braces, one group per element', _s, $initStart, 'E422'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Braced($d)&&(function(){   /* nested braces -> struct-element arrays */ if (!$aStructEl) fail('Nested brace initializers are for struct-element arrays', _s, $initStart, 'E422'); if (isNaN($aCount)) fail('An initialized struct-element array needs a literal size', _s, $initStart, 'E414'); var _arr = $d._; if (_arr.length > $aCount) { failSurplus($a.name, 'elements', _arr.length, $aCount, _arr[$aCount] && _arr[$aCount].at, _s, $initStart); } $binit = []; for (var _ae = 0; _ae < $aCount; ++_ae) { var _aev = (_ae < _arr.length) ? indexedEntry(_arr[_ae], _s, $initStart) : undefined; buildStructInit($aStruct, (_aev && _aev.braced) || [], $binit, _s, $initStart); } emitInitData($binit, _s, $initStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Braced($){var $i=createParserContext();return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return BracedEntry($i)&&(function(){ $._[$.n++] = $i._; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&BracedEntry($i)&&(function(){ $._[$.n++] = $i._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BracedEntry($){var $fname,$fat,$id=createParserContext(),$e=createParserContext();return (function(){var _b=_i;return (function(){ $fname = undefined; $fat = _i; ; return true})()&&((function(){var _b=_i;return Identifier($id)&&(_s[_i]===":")&&(++_i,true)&&_($)&&(function(){ $fname = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&BracedItem($e)&&(function(){ var _v = $e._;   /* bare `$e._` is the VALUE; `$e.field` would set a CONTEXT property, and `Braced` stores only the value */ _v.field = $fname; _v.at = $fat; $._ = _v; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BracedItem($){var $b=createParserContext(),$x=createParserContext();return (function(){var _b=_i;return Braced($b)&&(function(){ $._ = { braced: $b._ }; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($x)&&(function(){ var m = metaSlot($x._); var op = makeRValue(m, '#<&'); if (span(op[0] || '', '#<&') !== 1) fail('Initializer must be a constant', _s, _i, 'E407'); $._ = { op: op, type: m.type }; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function InitList($){var $d,$type,$entryAt,$x=createParserContext();return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ $d = ' '; $type = undefined; ; return true})()&&((function(){var _b=_i;return (function(){var _l=_i,_x=(function(){var _b=_i;return Identifier($)&&(_s[_i]===":")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()&&(function(){ $entryAt = _i; ; return true})()&&Expr($x)&&(function(){ var xMeta = metaSlot($x._); $type = (initElemType !== undefined ? initElemType : xMeta.type); $d += makeConstant(xMeta, $type, _s, $entryAt); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){var _l=_i,_x=(function(){var _b=_i;return Identifier($)&&(_s[_i]===":")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()&&(function(){ $entryAt = _i; ; return true})()&&Expr($x)&&(function(){ var xMeta = metaSlot($x._); var xType = (initElemType !== undefined ? initElemType : xMeta.type); var constant = makeConstant(xMeta, xType, _s, $entryAt); /* decide if we need to flush DATA */ if (  constant[0] === '<' || $d[1] === '<' || ($d + ' ' + constant).length >= 55) { declare( initElemRow(), 'globals', undefined, xType, true, $d.substr(1), _s, _i ); $d = ''; } $d += ' ' + constant; $type = xType; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ if ($d.substr(1) !== '') { declare( initElemRow(), 'globals', undefined, $type, true, $d.substr(1), _s, _i ); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArgsDecl($){var $v=createParserContext();return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return VarDecl($v)&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($v)&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LocalsDecl($){var $v=createParserContext();return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return (function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TypeBase($){var $t=createParserContext(),$id=createParserContext();return (function(){var _b=_i;return BASE_TYPE($t)&&_($)&&(function(){ $._ = CASTS_TO_TYPES[$t._]; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Identifier($id)&&(function(){ if (!isStructAtom($id._) && !isFuncTypeAtom($id._)) fail('Unknown type ' + $id._, _s, _i, 'E413'); $._ = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function VarDecl($){var $base=createParserContext(),$desc,$nameStart,$id=createParserContext();return (function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ $nameStart = _i; ; return true})()&&Identifier($id)&&(function(){ checkReservedName($id._, 'variable', _s, $nameStart); var head = descHead($desc); var tail = descTail($desc); if (tail === undefined && isStructAtom(head)) { $.type = 'S'; $.struct = head; $.elem = undefined; $.words = structWords(head); } else if (tail === undefined && isFuncTypeAtom(head)) { $.type = 'F';           /* named funcptr type -> a funcptr carrying its type tag */ $.elem = head; $.struct = undefined; $.words = undefined;    /* scalar funcptr: a single word, no size operand */ } else { $.type = head; $.elem = tail; $.struct = undefined; $.words = undefined; } $.name = $id._; $.size = undefined; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArrayDecl($){var $desc,$extent,$base=createParserContext(),$nameStart,$id=createParserContext(),$extentStart,$x=createParserContext(),$size;return (function(){var _b=_i;return (function(){ $desc = undefined; $extent = undefined; ; return true})()&&((function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&ARRAY($)&&_($)&&(function(){ $nameStart = _i; ; return true})()&&Identifier($id)&&(function(){ checkReservedName($id._, 'array', _s, $nameStart); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="[")&&(++_i,true)&&_($)&&(function(){ $extentStart = _i; ; return true})()&&Expr($x)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ $size = makeConstant($x._, 'i', _s, $extentStart); /* A NEGATIVE extent runs the layout BACKWARDS. On a plain array the assembler catches it, because the count reaches it as a `GLOB *size` / `LOCA *size` operand it type-checks ("Incompatible types: .z.g"). A struct FIELD's count is only ever ADDED into the offset accumulator, which subtracts without complaint - `struct T { int a; int array b[-1]; int c }` compiles, assembles, runs, and puts `a` and `c` in the SAME WORD. Rejected here, at the declaration, because that is where the extent is still a number and where the invariant belongs. Zero is left alone: it wastes a field but aliases nothing, and every constant index into it is already out of range. */ if (constInt($size) < 0) fail('Array extent is negative: ' + dropHash($size), _s, $extentStart, 'E462', 'an array holds zero or more elements'); $extent = dropHash($size);   /* element count - may be a symbolic const */ /* THE CONSUMER OWNS THE BORROW. A folded extent lives in a `<X>` scratch, and a scratch is recycled on the next borrow - so whoever still has to READ this one decides when it goes back: endStruct after the layout block, FuncDecl after the locals pass, GlobalDecl right after its declaration. Freeing it here instead let the NEXT declarator in the same list borrow the same scratch and overwrite the extent, which is how two array locals in one clause silently got whichever count was folded last. */ ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ /* An `extern struct` field states NO extent, matching the `extern array` rule: the host owns that layout, so a number here would be an unverifiable claim Impala never reads (offsets are host-supplied `.o.` symbols, 1-D stride is 1). */ var externField = (openStruct !== undefined && isExternStruct(openStruct)); if (externField && $extent !== undefined) { fail('An extern struct array field must not state a size', _s, _i, 'E430', 'the host owns this layout - write `array ' + $id._ + '` without a size'); } if (!externField && $extent === undefined) { fail('Array ' + $id._ + ' needs a size', _s, _i, 'E431', 'only a sizeless `extern array` or `extern struct` field may omit it'); } $.type = 'A'; $.elem = $desc; $.name = $id._; $.size = $extent; $.words = $extent;                   /* an array's words ARE its count; a struct element scales symbolically, in arrayAllocSize */ ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Statement($){var $lblStart,$label=createParserContext();return (function(){var _b=_i;return (function(){ var snippet = _s.substr(_i); var cut     = find(snippet, "{;\r\n"); var txt     = (cut >= 0 ? snippet.substr(0, cut) : snippet); emitMeta({ operator:';', type:undefined, operands:[ txt, undefined, undefined ] }); ; return true})()&&((function(){while((function(){var _b=_i;return (function(){ $lblStart = _i; ; return true})()&&Identifier($label)&&(_s[_i]===":")&&(++_i,true)&&_($)&&(function(){ emitMeta({ operator:'<--', type:undefined, operands:[ '@' + $label._, undefined, undefined ] }); /* carry the source position so processBranches can name the `.impala` line if this label is a duplicate */ var lbl = metacode[metacode.length - 1]; lbl.labelSource = _s; lbl.labelOffset = _i; /* the 1.x `goto break;` early-exit idiom is the --legacy case */ checkReservedName($label._, 'label', _s, $lblStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Assert($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Block($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Copy($)||(_im=(_i>_im?_i:_im),_i=_b,false)||DoWhile($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Loop($)||(_im=(_i>_im?_i:_im),_i=_b,false)||For($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Goto($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Return($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Break($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Continue($)||(_im=(_i>_im?_i:_im),_i=_b,false)||If($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Switch($)||(_im=(_i>_im?_i:_im),_i=_b,false)||While($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Destructure($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ if (metaSlot($._).winBase !== undefined) {     /* discarded struct-return value: free its window + base */ returnBack(metaSlot($._).base); freeStructWindow($._); } releaseMeta($._); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Expr($){var $r=createParserContext();return (function(){var _b=_i;return Bitwise($)&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($r)&&(function(){ if (!dry) assign($._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Bitwise($){var $first,$op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return (function(){ $first = undefined; ; return true})()&&AddSub($)&&((function(){while((function(){var _b=_i;return BITWISE_OP($op)&&_($)&&AddSub($r)&&(function(){ if (!dry) { if ($first === undefined) $first = $op._; else if ($first !== $op._) mixedBitwise($first, $op._, _s, _i); binaryOp($op._, $._, $r._, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ if (!dry) stampBitwise($._, $first !== undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function AddSub($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return MulDiv($)&&((function(){while((function(){var _b=_i;return ADDSUB_OP($op)&&_($)&&MulDiv($r)&&(function(){ if (!dry) binaryOp($op._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function MulDiv($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return PrePost($)&&((function(){while((function(){var _b=_i;return MULDIV_OP($op)&&_($)&&PrePost($r)&&(function(){ if (!dry) mulDivOp($op._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function PrePost($){var $op=createParserContext(),$cdesc,$ccast,$sid=createParserContext(),$pdepth;return (function(){var _b=_i;return (function(){var _b=_i;return PREFIX_OP($op)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="(")&&(++_i,true)&&_($)&&BASE_TYPE($op)&&_($)&&(function(){ $cdesc = CASTS_TO_TYPES[$op._]; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $cdesc = 'p:' + $cdesc; $ccast = 'pointer'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="(")&&(++_i,true)&&_($)&&Identifier($sid)&&(function(){ $pdepth = 0; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $pdepth = $pdepth + 1; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ /* With no `pointer` this is only a cast when the name is a FUNCTYPE, which is already a pointer and so needs no modifier - a struct has no by-value cast, and anything else is a plain parenthesized expression. Backing out (rather than failing) lets Value parse `(x) + 1`; the test above is a pure lookup, so there is no side effect to undo. */ if ($pdepth === 0) { if (!isFuncTypeAtom($sid._)) return false; $cdesc = 'F:' + $sid._; $ccast = 'funcptr'; } else { if (!isStructAtom($sid._) && !isFuncTypeAtom($sid._)) fail('Unknown type ' + $sid._, _s, _i, 'E413'); $cdesc = $sid._; for (var _pk = 0; _pk < $pdepth; ++_pk) $cdesc = 'p:' + $cdesc; $ccast = 'pointer'; } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&PrePost($)&&(function(){ if (!dry) { if ($ccast) {          /* a named/pointer cast; a bare BASE_TYPE cast has no $ccast */ unaryOp($ccast, $._, _s, _i); setElem($._, descTail($cdesc)); } else { unaryOp($op._, $._, _s, _i); } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Value($)&&((function(){while((function(){var _b=_i;return FuncCall($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Subscript($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FieldAccess($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Subscript($){var $idxAt,$s=createParserContext();return (function(){var _b=_i;return (_s.substr(_i,2)==="[[")&&(_i+=2,true)&&_($)&&(function(){ $idxAt = _i; ; return true})()&&Expr($s)&&(_s.substr(_i,2)==="]]")&&(_i+=2,true)&&_($)&&(function(){ if (!dry) { var sbs = metaSlot($._); if (!subscriptScales(sbs)) fail('Scaled subscript on a one-word element', _s, _i, 'E205', '`[[ ]]` scales by the element size - this element is one word, so write `[ ]`'); subscriptStruct($._, $s._, _s, $idxAt); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="[")&&(++_i,true)&&_($)&&(function(){ $idxAt = _i; ; return true})()&&Expr($s)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ if (!dry) { var sb = metaSlot($._); if (subscriptScales(sb)) fail('Plain subscript on a struct element', _s, _i, 'E204', 'this element is a struct, so the index scales - write `[[ ]]`'); if ((sb.place && sb.arrayOf) || (sb.type === 'p' && isStructAtom(sb.elem))) subscriptStruct($._, $s._, _s, $idxAt); else binaryOp('=[]', $._, $s._, _s, $idxAt); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FieldAccess($){var $f=createParserContext();return (function(){var _b=_i;return (_s.substr(_i,2)==="->")&&(_i+=2,true)&&_($)&&Identifier($f)&&(function(){ if (!dry) fieldAccess($._, $f._, true, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===".")&&(++_i,true)&&_($)&&Identifier($f)&&(function(){ if (!dry) fieldAccess($._, $f._, false, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncCall($){var $type,$;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ if (!dry) { $.count = 0; /* how many leading output slots the callee expects (>1 = multi-return) */ var _c = metaSlot($._); var _rs = 1; if (_c.operator === ':=' && _c.operands[1] && (_c.operands[1][0] === '&' || _c.operands[1][0] === '^')) { var _e = symbols.functions[_c.operands[1].substr(1)]; if (_e && _e.signature && _e.signature.returnWords !== undefined && _e.signature.returnWords > 1) _rs = _e.signature.returnWords;   /* multi-scalar OR by-value struct return window */ } else if (_c.type === 'F' && isFuncTypeAtom(_c.elem)) { var _ft = functypes[_c.elem];   /* indirect call through a named funcptr type */ if (_ft.returnWords > 1) _rs = _ft.returnWords; } $.retSlots = _rs; $.words = 0;                            /* input words placed so far (struct args span >1) */ $.base  = borrowForCall(); for (var _os = 1; _os < _rs; ++_os)      /* reserve the extra output slots */ claimSlot($.base + _os); $.types = []; $.elems = []; $.opnds = []; } ; return true})()&&((function(){var _b=_i;return Argument($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&Argument($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){var _b=_i;return (_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){   /* a '(' after a value is always a call, so no valid parse ever backtracks out of one - and the prologue above already borrowed the call window, which a backtrack would leak into whatever diagnostic comes next. Reject here, where the syntax broke. */ if (!dry) fail('Malformed argument list', _s, _i, 'E442', 'expected , or ) here - and note that a comparison or a && / || group is not a value in Impala'); ; return true})()&&(function(){var _l=_i,_x=_($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ if (!dry) { var callee = metaSlot($._); var callResultType = '?'; var signature = null; var calleeName = null; if (span(callee.type, 'FN') !== 1) { typeError( 'Invalid type for function call ({$type1})', _s, _i, callee.type , undefined, 'E408'); } if (callee.operator === ':=' && callee.operands[1] && (callee.operands[1][0] === '&' || callee.operands[1][0] === '^')) { calleeName = callee.operands[1].substr(1); var entry = symbols.functions[calleeName]; /* an Impala-defined function, or an extern with a DECLARED prototype (name-only externs carry no `params` and stay unchecked - they assert nothing) */ if (entry && entry.signature && (entry.kind === 'FUNC' || entry.signature.params)) { signature = entry.signature; } } else if (callee.type === 'F' && isFuncTypeAtom(callee.elem)) { signature = functypes[callee.elem];   /* indirect call: check against the funcptr type */ } if (signature) { var params = signature.params || []; var actualCount = ($.types ? $.types.length : 0); var expectedCount = params.length; var label = (calleeName || 'function'); if (actualCount !== expectedCount) { fail( 'Invalid argument count when calling ' + label + ' (expected ' + expectedCount + ', got ' + actualCount + ')', _s, _i , 'E405'); } for (var argIdx = 0; argIdx < expectedCount; ++argIdx) { var expected = params[argIdx].type; var actual = $.types[argIdx]; if (actual === undefined) { actual = '?'; } if (actual === '?' || expected === undefined) { continue; } if (actual !== expected) { /* Name the struct when the actual is a struct VALUE, and point at `&`: passing `v` where `V pointer` is wanted is the common slip now that by-value struct params are parked for Impala 3.0. */ var _actualText = ((actual === 'S' && $.elems && $.elems[argIdx]) ? 'struct ' + $.elems[argIdx] : '{$type1}'); typeError( 'Argument type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (' + _actualText + ' vs expected {$type2})', _s, _i, actual, expected , 'E406', ((actual === 'S' && expected === 'p') ? 'pass its address with & (by-value struct params are parked for Impala 3.0)' : undefined)); } if (expected === 'S' && params[argIdx].struct !== undefined && $.elems[argIdx] !== params[argIdx].struct) { fail('Struct type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (expected ' + params[argIdx].struct + ', got ' + ($.elems[argIdx] || 'a non-struct value') + ')', _s, _i, 'E421'); } var expectedElem = params[argIdx].elem;   /* typed pointer param: assume loudly */ if (expected === 'p' && expectedElem !== undefined && $.opnds[argIdx] !== '&NULL' && $.elems[argIdx] !== expectedElem) { fail('Pointer element type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (expected ' + elemVerbose(expectedElem) + ' elements, got ' + elemVerbose($.elems[argIdx]) + ' elements)', _s, _i, 'E202', 'use a cast: (' + elemVerbose(expectedElem) + ' pointer)'); } /* Same rule for a named funcptr param as for assignment, so the same check: `expected` is already 'F' here, which is what the assign path derives from the r-value's own type. */ if (expected === 'F' && isFuncTypeAtom(expectedElem)) { checkFuncPtrTarget(expectedElem, $.opnds[argIdx], 'F', $.elems[argIdx], ' for argument ' + (argIdx + 1) + ' when calling ' + label, _s, _i); } } if (signature.returnResolved && signature.returns !== undefined) { callResultType = signature.returns; } else if (signature.expectedReturn !== undefined) { callResultType = signature.expectedReturn; } else if (signature.returns !== undefined) { callResultType = signature.returns; } } var callComment = formatCallExpectationComment( calleeName, signature, $.types, callResultType, sourceName, _s, _i,                 /* the CALL SITE, not the enclosing declaration */ $.elems ); var commentIndex = -1; if (callComment) { commentIndex = metacode.length; emit(';', undefined, callComment, undefined, undefined); commentIndex = metacode.length - 1; } var func = makeRValue(callee, '&^$%'); emit('()', '?', func, '%' + $.base, '*' + ($.words + $.retSlots)); returnBack(func); while ($.words-- > 0) {              /* free the argument words (past the output slots) */ returnBack('%' + ($.base + $.retSlots + $.words)); } makeMeta(callee, ':=', callResultType, undefined, '%' + $.base, undefined); /* Keep the RETURN's element type: `returns V pointer` must yield a V-pointer, not a bare one, or `*f()` cannot be recognised as a struct and typed-pointer assignment checks go blind. A funcptr type carries returnElem too, so indirect calls work. */ setElem(callee, signature ? signature.returnElem : undefined); if (signature && signature.returns === 'S') {   /* by-value struct return -> a place over the output window */ var _wp = borrow('%'); emit('=&', 'p', _wp, '%' + $.base, '*' + $.retSlots);   /* numeric: fixed output window (see copyStructArg) */ setPlace(callee, 'pointer', _wp, [], signature.returnStruct, signature.returnStruct); callee.winBase  = $.base;       /* output window slots to free once the value is consumed */ callee.winWords = $.retSlots; } else if ($.retSlots > 1) {        /* multi-return: expose window for destructuring */ callee.multiBase = $.base; callee.multiCount = $.retSlots; callee.multiReturnList = signature.returnList; } if (calleeName) { callee.callInfo = { name: calleeName, commentIndex: commentIndex, commentArgs: { name: calleeName, signature: signature, actualTypes: ($.types ? $.types.slice() : undefined), actualElems: ($.elems ? $.elems.slice() : undefined), sourceName: sourceName, sourceCode: _s, sourceOffset: _i } }; } else if (callee.callInfo) { callee.callInfo = undefined; } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Argument($){var $a=createParserContext(),$type;return (function(){var _b=_i;return Expr($a)&&(function(){ if (!dry) { ++$.count; var meta = metaSlot($a._); checkIndexUse(meta);   /* a bare arg is placed without makeRValue */ if (meta.type === 'V') { typeError( 'Invalid type ({$type1})', _s, _i, meta.type, undefined, 'E406', 'a function with no `returns` clause produces no value' ); } if ($.types) { $.types.push(meta.type); } if ($.elems) {                       /* element chain + null-ness, captured */ $.elems.push(meta.elem);         /* before makeArgValue mutates the meta */ $.opnds.push(bareOperand(meta));  /* `&NULL` marks a null/nullfunc literal */ } var winSlot = $.base + $.retSlots + $.words; if (meta.type === 'S') {              /* by-value struct argument spans sizeof words */ var w = structWords(meta.struct); copyStructArg($a._, winSlot, w); $.words += w; } else { makeArgValue($a._, winSlot); $.words += 1; } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Group($){return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if (!dry) stampBitwise($._, false); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BoolGroup($){var $label;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ $label = undefined; ; return true})()&&And($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="||")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('t'); } emit('?->', true, $label, undefined, undefined); ; return true})()&&And($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if ($label !== undefined) { emit('<-?', true, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function And($){var $label;return (function(){var _b=_i;return (function(){ $label = undefined; ; return true})()&&Comp($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="&&")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('f'); } emit('?->', false, $label, undefined, undefined); ; return true})()&&Comp($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ if ($label !== undefined) { emit('<-?', false, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Comp($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return (_s[_i]==="!")&&(++_i,true)&&_($)&&(function(){ /* `!` sits BELOW comparison, so `!x == 2` means `!(x == 2)` - the opposite of the C reading. Require its operand to be parenthesised (or another `!`); reuse the strict-expression machinery so `--legacy` keeps the old meaning with a warning. */ var c = _s.charAt(_i); if (c !== '!' && c !== '(') strictError( "'!' binds below comparison; parenthesise its operand", _s, _i, 'E103', 'write !(a == b), or to negate a value use (a) != b'); ; return true})()&&Comp($)&&(function(){ emit('!', undefined, undefined, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = true; ; return true})()&&(function(){var _l=_i,_x=Group($);_i=_l;return !_x})()&&(function(){ dry = false; ; return true})()&&BoolGroup($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = false; ; return true})()&&Expr($)&&COMP_OP($op)&&_($)&&Expr($r)&&(function(){ checkCompMix($._, $r._, _s, _i); binaryOp($op._, $._, $r._, _s, _i); emitMeta($._); releaseMeta($._); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Assert($){var $okLabel,$start,$exprText,$str=createParserContext(),$;return (function(){var _b=_i;return ASSERT($)&&_($)&&(function(){ $okLabel  = beginDebugGuard('a'); $start    = _i; $exprText = ''; ; return true})()&&BoolGroup($str)&&(function(){   /* the assert's own source text becomes its message */ $exprText = _s.substring($start, _i); $exprText = $exprText.replace(/[ \t\r\n]+$/, ''); ; return true})()&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ endDebugGuard($okLabel, $str._, $exprText, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Block($){return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while(Statement($));})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Goto($){var $gotoStart,$label=createParserContext();return (function(){var _b=_i;return GOTO($)&&_($)&&(function(){ $gotoStart = _i; ; return true})()&&Identifier($label)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ emit('-->', undefined, '@' + $label._, undefined, undefined); /* carry the source position so processBranches can name the `.impala` line if this label is never defined (E445) */ var g = metacode[metacode.length - 1]; g.gotoSource = _s; g.gotoOffset = $gotoStart; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Return($){return (function(){var _b=_i;return RETURN($)&&_($)&&(function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)&&(function(){   /* bare `return;` is an early exit - RETU returns whatever the slot holds */ emit('--^', undefined, undefined, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){   /* `return expr;` - Impala returns via the named return variable. The failed `';'_` reset the cursor, so _i is back at the value. */ fail('return does not take a value - assign to the return variable instead', _s, _i, 'E448', 'a `returns T r` function returns whatever `r` holds; write `r = expr;` then `return;`'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Break($){var $start;return (function(){var _b=_i;return (function(){ $start = _i; ; return true})()&&(_s.substr(_i,5)==="break")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()&&_($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ fail("'break' is not supported: a switch arm already does not fall through, and a loop is left with `goto` to a label after it", _s, $start, 'E450', 'replace `break;` with `goto` to a label placed after the loop'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Continue($){var $start;return (function(){var _b=_i;return (function(){ $start = _i; ; return true})()&&(_s.substr(_i,8)==="continue")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()&&_($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ fail("'continue' is not supported: jump to a label at the end of the loop body with `goto`", _s, $start, 'E450', 'replace `continue;` with `goto` to a label at the loop-body end'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function If($){var $dontLabel,$,$doneLabel;return (function(){var _b=_i;return IF($)&&_($)&&BoolGroup($)&&(function(){ $dontLabel = newLabel('f'); emit('?->', false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){   /* `if (c) { } ; else` - the `;` is a complete empty statement, so the `if` ends there and `else` is left dangling. That fell through to a bare E001 pointing PAST the `else`; name the real culprit. Guard on the `;` first so the window scan is not per-statement. */ if (_s.charAt(_i) === ';' && /^;[ \t\r\n]*else(?![A-Za-z_$0-9])/.test(_s.substr(_i, 32))) { fail('a `;` here ends the `if`, leaving the `else` with nothing to attach to', _s, _i, 'E451', 'remove the `;` before `else` - a block or statement body takes no extra terminator'); } ; return true})()&&(function(){var _b=_i;return ELSE($)&&_($)&&(function(){ $doneLabel = newLabel('e'); emit('-->', undefined, $doneLabel, undefined, undefined); emit('<-?',  false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('<--', undefined, $doneLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ emit('<-?', false, $dontLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DoWhile($){var $loopLabel;return (function(){var _b=_i;return DO($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<-?', false, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&WHILE($)&&_($)&&BoolGroup($)&&(function(){ emit('?->', true, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Loop($){var $loopLabel;return (function(){var _b=_i;return LOOP($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function For($){var $varStart,$var=createParserContext(),$gotInit,$init=createParserContext(),$toExpr=createParserContext(),$type,$to,$noLoopLabel,$loopLabel,$body=createParserContext();return (function(){var _b=_i;return FOR($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ $varStart = _i; ; return true})()&&Variable($var)&&(function(){ /* loop-variable must be local, modifiable int / pointer */ var varMeta = metaSlot($var._); if (varMeta.operator !== '=' || span(varMeta.type, "ip") === 0) { fail( 'For variable must be a local modifiable int or pointer variable', _s, $varStart , 'E305', 'a parameter, global or non-scalar cannot be the loop variable - copy it into a `locals` int or pointer and loop over that'); } /* `FORp` steps exactly one WORD and is already a 3-operand form, so a struct pointer has nowhere to put its stride. Scaling only the bound silently ran sizeof(S) times too many (F2 in docs/Impala2Review.md). */ if (strideStruct(varMeta.elem) !== undefined) { fail( 'For variable must not be a struct pointer', _s, _i, 'E309', 'FORp cannot stride by a struct - use `while (p < end) { ...; p = &p[[1]]; }`'); } $gotInit = false;            /* flag to detect an explicit start value */ ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($init)&&(function(){ assign($init._, $var._, $init._, _s, _i); $gotInit = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&TO($)&&_($)&&Expr($toExpr)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var varMeta  = metaSlot($var._); var toMeta   = metaSlot($toExpr._); if (toMeta.type !== varMeta.type) { typeError( 'Incompatible types ({$type1} and {$type2})', _s, _i, varMeta.type, toMeta.type , 'E301'); } /* constant upper bound */ $to = makeRValue(toMeta); /* initial comparison  (var < to)                         */ emit( '<', toMeta.type, undefined, $gotInit ? metaSlot($init._).operands[1]     /* start value from “var = expr” */ : varMeta.operands[1],            /* or the original variable */ $to ); if ($gotInit) { releaseMeta($init._); } /* branch-out   and  loop label */ $noLoopLabel = newLabel('e'); emit('?->', false, $noLoopLabel, undefined, undefined); $loopLabel   = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($body)&&(function(){ var varMeta = metaSlot($var._); /* increment + jump back. A struct pointer never reaches here - E309 above rejects it, because `FORp` is already a 3-operand form with nowhere to put a stride (F2 in docs/Impala2Review.md). */ emit( '...', varMeta.type, varMeta.operands[1],        /* address of loop variable */ $to, $loopLabel ); emit('<-?', false, $noLoopLabel, undefined, undefined); returnBack($to); releaseMeta(varMeta); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Copy($){var $l=createParserContext(),$f=createParserContext(),$t=createParserContext(),$length,$type,$;return (function(){var _b=_i;return COPY($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($l)&&FROM($)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var fromMeta = metaSlot($f._); var toMeta   = metaSlot($t._); $length = makeConstant($l._, 'i', _s, _i); var lengthHash = dropHash($length); if (fromMeta.type + toMeta.type !== 'pp') { returnBack($length); typeError( 'Invalid types ({$type1} and {$type2})', _s, _i, fromMeta.type, toMeta.type , 'E301'); } var copyMeta = metaSlot($l._); makeMeta( copyMeta, 'copy', '?', makeRValue(toMeta, '&$%'), makeRValue(fromMeta, '&$%'), '*' + lengthHash ); emitMeta(copyMeta); returnBack($length); releaseMeta(copyMeta); ; return true})()&&(_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Destructure($){return (function(){var _b=_i;return DestTarget($)&&((function(){for(var _n=0;(function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&DestTarget($)||(_im=(_i>_im?_i:_im),_i=_b,false)})();++_n);return _n>0})())&&(_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){   /* PARKED for Impala 3.0 - see docs/ParkedFeatures.md. Kept only to recognise the shape and reject it well; the targets themselves are never needed. */ fail('Destructuring assignment is not supported in Impala 2.0', _s, _i, 'E429', 'assign one value per statement'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DestTarget($){var $tgtGlobal,$id=createParserContext(),$tgtName;return (function(){var _b=_i;return (function(){ $tgtGlobal = false; ; return true})()&&((function(){var _b=_i;return GLOBAL($)&&_($)&&(function(){ $tgtGlobal = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&Identifier($id)&&(function(){ $tgtName = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Switch($){var $f=createParserContext(),$t=createParserContext(),$size,$switcher,$,$switchExit,$progress,$stmt=createParserContext();return (function(){var _b=_i;return SWITCH($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s.substr(_i,2)==="==")&&(_i+=2,true)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(function(){ var switchMeta = metaSlot($._); /* the switch expression must be an int */ if (switchMeta.type !== 'i') { fail('Switch expression needs to be int', _s, _i, 'E306'); } /* lower bound (compile-time constant) */ switchMeta.from = makeConstant($f._, 'i', _s, _i); /*    size = to - from   */ $size = subConstInt( makeConstant($t._, 'i', _s, _i), switchMeta.from ); /*   switcher = (expr − from)   */ $switcher = subConstInt( makeRValue(switchMeta, '$%'), switchMeta.from ); /* snapshot the range as plain numbers now: the operands are handed back to the scratch pool below, and a case is only checkable while both ends are known. */ switchMeta.fromNum = constInt(switchMeta.from); switchMeta.sizeNum = constInt($size); switchMeta.caseSeen = {}; switchMeta.switchLabel = newLabel('s'); $switchExit              = newLabel('e'); switchStack.push(switchMeta); emit( '-->#', switchMeta.type, $switcher, '*' + dropHash($size), switchMeta.switchLabel ); returnBack($switcher); returnBack($size); $progress = undefined;       /* track case / default presence */ ; return true})()&&(_s[_i]===")")&&(++_i,true)&&_($)&&(_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return CASE($)&&_($)&&(function(){ /* multiple CASE groups -> fall-through handled here */ if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } else { $progress = 'gotCases'; } /* dump the literal “case ...” comment */ var snippet = _s.substr(_i); var pos     = find(snippet, ":\r\n"); if (pos >= 0) { snippet = snippet.substr(0, pos); } emit( ';', undefined, 'case ' + snippet, undefined, undefined ); ; return true})()&&CaseExpr($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&CaseExpr($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DEFAULT($)&&_($)&&(function(){ if ($progress === 'gotDefault') { fail('Default case already defined', _s, _i, 'E409', 'a switch has at most one `default` arm'); } else if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } var ctx = switchStack[switchStack.length - 1]; emit(';',    undefined, 'default',       undefined, undefined); emit('<--',  undefined, ctx.switchLabel,  undefined, undefined); $progress = 'gotDefault'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(_s[_i]===":")&&(++_i,true)&&_($)&&Statement($stmt)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ var ctx = switchStack.pop() || metaSlot($._); /* no explicit “default” -> hook it up now                        */ if ($progress !== 'gotDefault') { emit('<--', undefined, ctx.switchLabel, undefined, undefined); } emit('<--', undefined, $switchExit, undefined, undefined); returnBack(ctx.from); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CaseExpr($){var $n;return (function(){var _b=_i;return Expr($)&&(function(){ /* offset = constant(expr) - switch.from                         */ var ctx      = switchStack[switchStack.length - 1]; var caseMeta = metaSlot($._); var baseFrom = (ctx ? ctx.from : caseMeta.from); var baseLabel = (ctx ? ctx.switchLabel : caseMeta.switchLabel); var caseConst = makeConstant(caseMeta, 'i', _s, _i); checkCaseValue(ctx, constInt(caseConst), _s, _i); $n = subConstInt(caseConst, baseFrom); /* create label for this case                                     */ emit( '<--', undefined, baseLabel + '#' + dropHash($n), undefined, undefined ); returnBack($n); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function While($){var $loopLabel,$exitLabel;return (function(){var _b=_i;return WHILE($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&BoolGroup($)&&(function(){ $exitLabel = newLabel('e'); emit('?->', false, $exitLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); emit('<-?', false, $exitLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Value($){var $base=createParserContext(),$f=createParserContext(),$i=createParserContext(),$s=createParserContext();return (function(){var _b=_i;return Group($)||(_im=(_i>_im?_i:_im),_i=_b,false)||SIZEOF($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&TypeBase($base)&&(function(){ if (!dry) { var head = descHead($base._); if (isStructAtom(head)) {   /* struct size -> symbolic .z.Name */ if (!isExternStruct(head) && !structDefined(head)) fail('sizeof of incomplete struct ' + head, _s, _i, 'E419'); makeMeta($._, ':=', 'i', undefined, '#.z.' + head, undefined); setElem($._, undefined); } else { makeMeta($._, ':=', 'i', undefined, '#1', undefined); setElem($._, undefined); } } ; return true})()&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FloatLiteral($f)&&(function(){ if (!dry) { makeMeta($._, ':=', 'f', undefined, '#' + $f._, undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||IntegerLiteral($i)&&(function(){ if (!dry) { makeMeta($._, ':=', 'i', undefined, '#' + $i._, undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||StringLiteral($s)&&(function(){ if (!dry) { makeString('s', $._, evaluate($s._), _s, _i); setElem($._, 'i');      /* string data is int words (Impala 2) */ /* string data lives in a readonly section, so `"abc"[0] = 1` used to compile and fail at GAZL load - mark it readonly so the E404 element-write check catches the store at the source line */ metaSlot($._).readonly = true; } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULL($)&&_($)&&(function(){ if (!dry) { makeMeta($._, ':=', 'p', undefined, '&NULL', undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULLFUNC($)&&_($)&&(function(){ if (!dry) { makeMeta($._, ':=', 'F', undefined, '&NULL', undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Variable($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Variable($){var $global,$idStart,$id=createParserContext();return (function(){var _b=_i;return (function(){ $global = false; ; return true})()&&((function(){var _b=_i;return GLOBAL($)&&_($)&&(function(){ $global = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ $idStart = _i; ; return true})()&&Identifier($id)&&(function(){ if (!dry) { lookup($._, $id._, $global, _s, $idStart); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Identifier($){return (function(){var _b=_i;return (function(){var _l=_i,_x=KEYWORD($);_i=_l;return !_x})()&&(function(){var _m=_i;return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$".indexOf(_s[_i])>=0)&&(++_i,true)&&((function(){while(SYMBOL_CHAR($));})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FloatLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&(_s[_i]===".")&&(++_i,true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&((function(){var _b=_i;return (function(){var _b=_i;return (_s[_i]==="E")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="e")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IntegerLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&(function(){var _b=_i;return (_s.substr(_i,2)==="0x")&&(_i+=2,true)&&((function(){for(var _n=0;HEX($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="'")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(_s[_i]==="'")&&(++_i,true);_i=_l;return !_x})()&&ASCII($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function StringLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="\"")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\"\\\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="\\")&&(++_i,true)&&(function(){var _b=_i;return (!!_s[_i]&&"\"\\bfnrt".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="u")&&(++_i,true)&&HEX($)&&HEX($)&&HEX($)&&HEX($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="\"")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function KEYWORD($){var _b=_i,_words=KEYWORD_WORDS,_word,_end,_x;for(var _k=0;_k<_words.length;++_k){_word=_words[_k];if(_s.substr(_i,_word.length)===_word){_i+=_word.length;_end=_i;_x=SYMBOL_CHAR($);_i=_end;if(!_x)return true;_i=_b;}}_im=(_i>_im?_i:_im);_i=_b;return false}
function ABS($){return (function(){var _b=_i;return (_s.substr(_i,3)==="abs")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ARRAY($){return (function(){var _b=_i;return (_s.substr(_i,5)==="array")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ASSERT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="assert")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CASE($){return (function(){var _b=_i;return (_s.substr(_i,4)==="case")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CONST($){return (function(){var _b=_i;return (_s.substr(_i,5)==="const")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COPY($){return (function(){var _b=_i;return (_s.substr(_i,4)==="copy")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DEFAULT($){return (function(){var _b=_i;return (_s.substr(_i,7)==="default")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DO($){return (function(){var _b=_i;return (_s.substr(_i,2)==="do")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ELSE($){return (function(){var _b=_i;return (_s.substr(_i,4)==="else")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function EXPORT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="export")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IMPORT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="import")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function EXTERN($){return (function(){var _b=_i;return (_s.substr(_i,6)==="extern")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FLOAT($){return (function(){var _b=_i;return (_s.substr(_i,5)==="float")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function INLINE($){return (function(){var _b=_i;return (_s.substr(_i,6)==="inline")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FLOOR($){return (function(){var _b=_i;return (_s.substr(_i,5)==="floor")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FOR($){return (function(){var _b=_i;return (_s.substr(_i,3)==="for")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FROM($){return (function(){var _b=_i;return (_s.substr(_i,4)==="from")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FTOI($){return (function(){var _b=_i;return (_s.substr(_i,4)==="ftoi")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FUNCPTR($){return (function(){var _b=_i;return (_s.substr(_i,7)==="funcptr")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FUNCTYPE($){return (function(){var _b=_i;return (_s.substr(_i,8)==="functype")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FUNCTION($){return (function(){var _b=_i;return (_s.substr(_i,8)==="function")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GLOBAL($){return (function(){var _b=_i;return (_s.substr(_i,6)==="global")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GOTO($){return (function(){var _b=_i;return (_s.substr(_i,4)==="goto")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IF($){return (function(){var _b=_i;return (_s.substr(_i,2)==="if")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function INT($){return (function(){var _b=_i;return (_s.substr(_i,3)==="int")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ITOF($){return (function(){var _b=_i;return (_s.substr(_i,4)==="itof")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LOCALS($){return (function(){var _b=_i;return (_s.substr(_i,6)==="locals")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LOOP($){return (function(){var _b=_i;return (_s.substr(_i,4)==="loop")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function NATIVE($){return (function(){var _b=_i;return (_s.substr(_i,6)==="native")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function NULL($){return (function(){var _b=_i;return (_s.substr(_i,4)==="null")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function NULLFUNC($){return (function(){var _b=_i;return (_s.substr(_i,8)==="nullfunc")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function POINTER($){return (function(){var _b=_i;return (_s.substr(_i,7)==="pointer")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function READONLY($){return (function(){var _b=_i;return (_s.substr(_i,8)==="readonly")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function RETURN($){return (function(){var _b=_i;return (_s.substr(_i,6)==="return")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function RETURNS($){return (function(){var _b=_i;return (_s.substr(_i,7)==="returns")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SWITCH($){return (function(){var _b=_i;return (_s.substr(_i,6)==="switch")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SIZEOF($){return (function(){var _b=_i;return (_s.substr(_i,6)==="sizeof")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function STRUCT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="struct")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TEMPORARY($){return (function(){var _b=_i;return (_s.substr(_i,9)==="temporary")&&(_i+=9,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TO($){return (function(){var _b=_i;return (_s.substr(_i,2)==="to")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function WHILE($){return (function(){var _b=_i;return (_s.substr(_i,5)==="while")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BITWISE_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,2)==="<<")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,3)===">>>")&&(_i+=3,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)===">>")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="|")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="|")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="&")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="&")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="^")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ADDSUB_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="+")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="-")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function MULDIV_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="*")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="/")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="/")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="%")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COMP_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,2)==="<=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)===">=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="!=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="==")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="<")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===">")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BASE_TYPE($){var $;return (function(){var _b=_i;return INT($)&&(function(){ $._ = 'int'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||FLOAT($)&&(function(){ $._ = 'float'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||FUNCPTR($)&&(function(){ $._ = 'funcptr'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||POINTER($)&&(function(){ $._ = 'pointer'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BUILT_IN($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,3)==="abs")&&(_i+=3,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,5)==="floor")&&(_i+=5,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,4)==="itof")&&(_i+=4,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,4)==="ftoi")&&(_i+=4,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function PREFIX_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="-")&&(++_i,true)&&(function(){var _l=_i,_x=(function(){var _b=_i;return (_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DIGIT($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="-")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="~")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="&")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="*")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||BUILT_IN($)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SYMBOL_CHAR($){return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function HEX($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789ABCDEFabcdef".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DIGIT($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ASCII($){return (function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function _($){return (function(){var _b=_i;return ((function(){while((function(){var _b=_i;return ((function(){for(var _n=0;(!!_s[_i]&&" \t\r\n".indexOf(_s[_i])>=0)&&(++_i,true);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)||COMMENT($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COMMENT($){return (function(){var _b=_i;return (_s.substr(_i,2)==="/*")&&(_i+=2,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(_s.substr(_i,2)==="*/")&&(_i+=2,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s.substr(_i,2)==="*/")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="//")&&(_i+=2,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\r\n".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function createParserContext() {
        return {
                _: { operator: undefined, type: undefined, operands: [ undefined, undefined, undefined ] }
        };
}
var _i=0,_im=0,_o=createParserContext();
_o.options=_hostOptions;
var _b=root(_o);
return [_b,_o._,(_b?_i:_im)];
});
function impalaCompiler(source, options) {
	var compilerOptions;
	if (typeof options === 'string') {
		compilerOptions = { sourceName: options };
	} else if (options) {
		compilerOptions = options;
	} else {
		compilerOptions = {};
	}
	return impalaCompilerImpl(source, compilerOptions);
}
if (typeof module !== 'undefined' && module.exports) {
	module.exports = impalaCompiler;
	module.exports.impalaCompiler = impalaCompiler;
	module.exports.default = impalaCompiler;
	module.exports.raw = impalaCompilerImpl;
}
