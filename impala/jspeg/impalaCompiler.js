var impalaCompiler = (function(_s) {

    'use strict';
    var $$parser = {};
    var parser = $$parser;
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


    /** template baker: "Hello {name}" → eval expressions in {…} */
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


    /* ————————————————————————————————
     *  Impala-JSPEG  ▸  core tables / helpers  (ES3)
     *  arrays now rely on .length / .push
     * ———————————————————————————————— */

    /* 1  constants & simple flags */
    var IMPALA_VERSION = '1.0';
    parser.IMPALA_VERSION = IMPALA_VERSION;
    var dry = false;
    parser.dry = dry;

    /* 2  make sure the buckets exist */
    var metacode = [];
    parser.metacode = metacode;
    var strings = { s:[], a:[] };
    parser.strings = strings;
    var labelCounter = 0;
    parser.labelCounter = labelCounter;
    var stock = { '%': [], '<': [] };
    parser.stock = stock;
    var counters = { '%': 0,  '<': 0  };
    parser.counters = counters;
    var randomId = 0;
    parser.randomId = randomId;
    var symbols = { 'locals': {}, 'globals': {}, 'functions': {}, 'defines': {} };
    parser.symbols = symbols;
    var switchStack = [];
    parser.switchStack = switchStack;
    var noForward = false;
    parser.noForward = noForward;

    /* 2b  shared mutable state helpers */
    var parserState = {
        metacode: metacode,
        strings: strings,
        switchStack: switchStack,
        stock: stock,
        counters: counters
    };
    var state = parserState;
    parser.state = state;

    function getMetacode() {
        return parserState.metacode;
    }
    parser.getMetacode = getMetacode;

    function appendMetacode(entry) {
        parserState.metacode.push(entry);
    }
    parser.appendMetacode = appendMetacode;

    function clearMetacode() {
        parserState.metacode.length = 0;
    }
    parser.clearMetacode = clearMetacode;

    function ensureStringTable(prefix) {
        var tables = parserState.strings;
        var table = tables[prefix];
        if (!table) {
            table = tables[prefix] = [];
        }
        if (!table.rlookup) {
            table.rlookup = {};
        }
        return table;
    }
    parser.ensureStringTable = ensureStringTable;

    function resetStrings() {
        var tables = parserState.strings;
        tables.s = [];
        tables.s.rlookup = {};
        tables.a = [];
        tables.a.rlookup = {};
    }
    parser.resetStrings = resetStrings;

    function getStockBucket(cls) {
        var buckets = parserState.stock;
        var bucket = buckets[cls];
        if (!bucket) {
            bucket = buckets[cls] = [];
        }
        return bucket;
    }
    parser.getStockBucket = getStockBucket;

    function clearStockBucket(cls) {
        var bucket = getStockBucket(cls);
        resetQueue(bucket);
        return bucket;
    }
    parser.clearStockBucket = clearStockBucket;

    function resetStock() {
        clearStockBucket('%');
        clearStockBucket('<');
    }
    parser.resetStock = resetStock;

    function getCounter(cls) {
        return parserState.counters[cls] || 0;
    }
    parser.getCounter = getCounter;

    function setCounter(cls, value) {
        parserState.counters[cls] = value;
        return value;
    }
    parser.setCounter = setCounter;

    function nextCounter(cls) {
        var value = getCounter(cls);
        parserState.counters[cls] = value + 1;
        return value;
    }
    parser.nextCounter = nextCounter;

    function resetCounters() {
        setCounter('%', 0);
        setCounter('<', 0);
    }
    parser.resetCounters = resetCounters;

    function pushSwitchContext(ctx) {
        parserState.switchStack.push(ctx);
        return ctx;
    }
    parser.pushSwitchContext = pushSwitchContext;

    function popSwitchContext() {
        return parserState.switchStack.pop();
    }
    parser.popSwitchContext = popSwitchContext;

    function peekSwitchContext() {
        var stack = parserState.switchStack;
        return stack[stack.length - 1];
    }
    parser.peekSwitchContext = peekSwitchContext;

    function clearSwitchStack() {
        parserState.switchStack.length = 0;
    }
    parser.clearSwitchStack = clearSwitchStack;

    /* 3  bulk-fill the lookup tables */
    var META_TO_GAZL = {
        '=':    'MOV?',  ':=':   'MOV?',
        '=itof':'iTOf',  '=ftoi':'fTOi', '=abs':'ABS?', '=floor':'FLOf',
        '=[]':  'PEEK',  '[]=':  'POKE',  '=[]$':'GETL',  '[]$=':'SETL',
        '=*':   'PEEK',  ':=*':  'PEEK',  '*=':  'POKE',  '=&':  'ADRL', 'copy':'COPY',
        '-->':  'GOTO',  '-->#': 'SWCH',  '...': 'FOR?', '()':  'CALL', '--^':'RETU',
        '|':    'IOR?',  '&':    'AND?',  '^':   'XOR?', '<<':  'SHL?', '>>>':'SHRu', '>>':'SHR?',
        '+':    'ADD?',  '-':    'SUB?',  '*':   'MUL?', '/':   'DIV?', '%':  'MOD?', 'd':'DIFp',
        '<=':   'LEQ?',  '<':    'LSS?',  '>=':  'GEQ?', '>':   'GRT?', '!=': 'NEQ?', '==':'EQU?',
        '!<=':  'GRT?',  '!<':   'GEQ?',  '!>=': 'LSS?', '!>':  'LEQ?', '!!=':'EQU?', '!==':'NEQ?'
    };
    parser.META_TO_GAZL = META_TO_GAZL;

    var SUPPORTED_OPS = {
        '=*p': '?', '=&f': 'p', '=&F': 'p', '=&i': 'p', '=&p': 'p', '=&?': 'p',
        '=-i': 'i', '=-f': 'f', '=~i': 'i',
        '=floatf': 'f', '=float?': 'f', '=funcptrF': 'F', '=funcptr?': 'F',
        '=inti': 'i', '=int?': 'i', '=pointerp': 'p', '=pointer?': 'p',
        '=absi': 'i', '=absf': 'f', '=itofi': 'f', '=ftoif': 'i', '=floorf': 'f',
        '|ii': 'i', '&ii': 'i', '^ii': 'i', '<<ii': 'i', '>>>ii': 'i', '>>ii': 'i',
        '+ii': 'i', '-ii': 'i', '*ii': 'i', '/ii': 'i', '%ii': 'i',
        '+ff': 'f', '-ff': 'f', '*ff': 'f', '/ff': 'f',
        '+pi': 'p', '-pi': 'p', '-pp': 'i',
        '=[]pi': '?',
        '<=ii': 'i', '<ii': 'i', '>=ii': 'i', '>ii': 'i', '!=ii': 'i', '==ii': 'i',
        '<=ff': 'f', '<ff': 'f', '>=ff': 'f', '>ff': 'f', '!=ff': 'f', '==ff': 'f',
        '<=pp': 'p', '<pp': 'p', '>=pp': 'p', '>pp': 'p', '!=pp': 'p', '==pp': 'p',
        '<=FF': 'F', '<FF': 'F', '>=FF': 'F', '>FF': 'F', '!=FF': 'F', '==FF': 'F'
    };
    parser.SUPPORTED_OPS = SUPPORTED_OPS;

    var CASTS_TO_TYPES = {
        'float': 'f', 'funcptr': 'F', 'int': 'i', 'pointer': 'p'
    };
    parser.CASTS_TO_TYPES = CASTS_TO_TYPES;

    var ZEROES = {
        'f': '#0.0', 'i': '#0', 'p': '&NULL', 'F': '&NULL'
    };
    parser.ZEROES = ZEROES;

    var TYPE_SUFFIXES = {
        'void': '', 'i': 'i', 'f': 'f', 'p': 'p', 'F': 'p', 'U': '',
        'N': '', 'A': 'A', '?': 'E'
    };
    parser.TYPE_SUFFIXES = TYPE_SUFFIXES;

    var VERBOSE_TYPES = {
        'i': 'int', 'f': 'float', 'p': 'pointer', 'F': 'funcptr',
        'U': 'function', 'N': 'native', 'A': 'array', '?': 'untyped'
    };
    parser.VERBOSE_TYPES = VERBOSE_TYPES;

    /* 4  label & metacode helpers */
    function newLabel(prefix) {
        var tag = (prefix === undefined ? '' : String(prefix));
        return '@.' + tag + (labelCounter++);
    }
    parser.newLabel = newLabel;

    /* push a deep-cloned record into metacode */
    function emitMeta(rec) {
        appendMetacode(clone(metaSlot(rec)));
    }
    parser.emitMeta = emitMeta;

    /* allocate new (empty) meta record, fill via makeMeta, then push */
    function emit(op, type, op0, op1, op2) {
        var slot = {};                // fresh object
        slot = makeMeta(slot, op, type, op0, op1, op2);  // user-supplied helper
        appendMetacode(slot);
    }
    parser.emit = emit;

    /* 5  portable replacement for ppeg.fail */
    function fail(error, source, offset) {
        function oneLine(s) { return replace(replace(replace(s,"\t",' '),"\r",' '),"\n",' '); }
        throw bake(error) + ' : ' +
              oneLine(source.substr(offset - 8, 8)) + ' <!!!!> ' +
              oneLine(source.substr(offset, 40));
    }
    parser.fail = fail;



    /* ---------------------------------------------------------
     *  Short-circuit / branch processing
     * --------------------------------------------------------- */
    function processBranches() {
        var target      = { false: null, true: null }; // last FALSE / TRUE dest labels
        var targetCond  = null;                        // current branch condition (true / false)
        var currentGoto = null;                        // last unconditional goto
        var aliases     = {};                          // label alias map

        /* walk metacode bottom-to-top */
        var meta = getMetacode();
        for (var i = meta.length - 1; i >= 0; --i) {
            var inst = meta[i];

            switch (inst.operator) {

                /* ———  branch on TRUE / FALSE  ——— */
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

                    if (target[t] != null) {        // label already chosen – make alias
                        aliases[lbl] = target[t];
                        inst.operator = (lbl[2] !== 'a' ? null : '<--'); // keep assert labels
                    } else {
                        target[t]    = lbl;
                        inst.operator = '<--';      // we retain the label
                    }
                    break;
                }

                /* ———  invert ( NOT )  ——— */
                case '!':
                    var tmp    = target.false;
                    target.false = target.true;
                    target.true  = tmp;
                    targetCond   = !targetCond;
                    inst.operator = null;
                    break;

                /* comment – ignore */
                case ';':
                    break;

                /* ———  unconditional GOTO  ——— */
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

                /* ———  comparison ops  ——— */
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

                /* ———  anything else breaks the chain  ——— */
                default:
                    target.false = target.true = currentGoto = null;
            }
        }
    }
    parser.processBranches = processBranches;

    /* ---------------------------------------------------------
     *  Pool / stock handling for transients    (‘%’, ‘<…>’)
     * --------------------------------------------------------- */

    /* assure no duplicates exist in a stock bucket */
    function validateStock(cls) {
        var seen = {};
        var stk  = getStockBucket(cls);
        for (var i = 0; i < stk.length; ++i) {
            var tok = stk[i];
            assert(!seen[tok], "duplicate token in stock: " + tok);
            seen[tok] = true;
        }
        return true;
    }
    parser.validateStock = validateStock;

    /* borrow one token from a stock bucket (or create a new one) */
    function borrow(cls) {
        assert(validateStock(cls));

        var stk = getStockBucket(cls);
        if (stk.length) {
            return stk.pop();                      // reuse
        }

        /* otherwise mint a fresh id */
        if (cls === '%') {
            return '%' + nextCounter('%');
        }
        if (cls === '<') {
            var idx = nextCounter('<');
            return '<' + String.fromCharCode('A'.charCodeAt(0) + idx) + '>';
        }
        throw new Error("unknown stock class " + cls);
    }
    parser.borrow = borrow;

    /* smart borrow for CALL args – first free id in last consecutive run */
    function borrowForCall() {
        /* same safety check the original did */
        assert(validateStock('%'));

        var stk = getStockBucket('%');

        /* empty ⇒ mint a brand-new one */
        if (stk.length === 0) {
            return nextCounter('%');
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

        /* duplicate-check, like the original assert(validate…)    */
        assert(validateStock('%'));
        return chosen;
    }
    parser.borrowForCall = borrowForCall;

    /* put a token back into its stock bucket */
    function returnBack(op) {
        if (op == null) {
            return;
        }
        var c = op[0];

        /* ordinary transients / compile-time vars */
        if (c === '%' || c === '<') {
            var stk = getStockBucket(c);
            if (stk.indexOf(op) === -1) stk.push(op);   // avoid dupes
        }
        /* special case “…:<” suffix ---------------------------------
           original test:  op{len-4 : 2} == ':<'
           -> two chars beginning 4 from the end                       */
        else if (op.length >= 4 && op.substr(op.length - 4, 2) === ':<') {
            // recurse with everything from (len-3) to end
            returnBack(op.substr(op.length - 3));
        }
    }
    parser.returnBack = returnBack;

    /* Align with the original PPEG helper while avoiding the reserved
       `return` identifier in generated JavaScript. */
    $$parser["return"] = returnBack;

    /* --------------------------------------------------------- *
     *  Debug helpers & meta-record construction / destruction   *
     * --------------------------------------------------------- */

    /* pretty-print one meta-instruction (only when it has op) */
    function debugPrintMeta(m) {
        m = metaSlot(m);
        if (m && m.operator != null) {
            console.log(
                '{' + m.operator + '}(' + m.type + ') {'
                     + m.operands[0] + '} {' + m.operands[1]
                     + '} {' + m.operands[2] + '}'
            );
        }
    }
    parser.debugPrintMeta = debugPrintMeta;

    /* lazily materialise a meta-record for any parse node */
    function metaSlot(node) {
        if (node == null) {
            return { operator: undefined, type: undefined,
                     operands: [ undefined, undefined, undefined ],
                     __meta: true };
        }
        if (node.__meta) {
            if (node.operands === undefined) {
                node.operands = [ undefined, undefined, undefined ];
            }
            return node;
        }

        var slot = node._;
        if (!slot || !slot.__meta) {
            var operands = (node.operands instanceof Array
                            ? node.operands
                            : [ undefined, undefined, undefined ]);
            slot = {
                operator: node.operator,
                type: node.type,
                operands: operands,
                __meta: true
            };
            node._ = slot;
        }

        if (slot.operands === undefined) {
            slot.operands = [ undefined, undefined, undefined ];
        }

        node.operator = slot.operator;
        node.type     = slot.type;
        node.operands = slot.operands;
        return slot;
    }

    /* overwrite the fields of an existing meta object */
    function normaliseVoid(value) {
        return value === null ? undefined : value;
    }

    function makeMeta(rec, op, type, op0, op1, op2) {
        rec = metaSlot(rec);
        rec.operator  = normaliseVoid(op);
        rec.type      = normaliseVoid(type);
        rec.operands  = [
            normaliseVoid(op0),
            normaliseVoid(op1),
            normaliseVoid(op2)
        ];
        return rec;
    }
    parser.makeMeta = makeMeta;

    /* release all three operands contained in a meta-record */
    function releaseMeta(meta) {
        meta = metaSlot(meta);
        for (var i = 2; i >= 0; --i) {
            returnBack(meta.operands[i]);
        }
    }
    parser.releaseMeta = releaseMeta;

    /* --------------------------------------------------------- *
     *  R-value helpers                                          *
     * --------------------------------------------------------- */

    /**
     * Convert an expression into an r-value, allocating a transient
     * when needed.  `classes` defaults to '#<&^$%'.
     */
    function makeRValue(expr, classes) {
        classes = classes || '#<&^$%';

        expr = metaSlot(expr);

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
    }
    parser.makeRValue = makeRValue;

    /**
     * Ensure an expression’s value ends up in the given
     * transient “%<number>”.
     */
    function makeArgValue(expr, number) {
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

        /* remove %<number> from the free list if present */
        var transientCounter = getCounter('%');
        if (transientCounter === number) {
            setCounter('%', transientCounter + 1);
        } else {
            assert(transientCounter > number);
            var stk = getStockBucket('%');
            for (var idx = stk.length - 1;
                 idx >= 0 && stk[idx] !== tgt;
                 --idx) {}
            assert(idx >= 0, "transient " + tgt + " must exist in stock");
            stk.splice(idx, 1);
        }

        expr.operands[0] = tgt;
        emitMeta(expr);
    }
    parser.makeArgValue = makeArgValue;

    /* --------------------------------------------------------- *
     *  Typed error helper                                       *
     * --------------------------------------------------------- */

    function typeError(desc, source, offset, type1, type2) {
        var message = replace(desc, '{$type1}',
                              VERBOSE_TYPES[type1]);
        if (type2 !== undefined) {
            message = replace(message, '{$type2}',
                               VERBOSE_TYPES[type2]);
        }
        fail(message, source, offset);
    }
    parser.typeError = typeError;

    /* --------------------------------------------------------- *
     *  Binary operations ( + – * / [] etc. )                    *
     * --------------------------------------------------------- */
    function binaryOp(operator, leftx, rightx,
                      sourceCode, sourceOffset) {

        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        /* validate operand-type combination */
        var sig = operator + leftx.type + rightx.type;
        var tp  = SUPPORTED_OPS[sig];
        if (tp === undefined) {
            typeError(
                'Invalid types ({$type1} and {$type2})',
                sourceCode, sourceOffset,
                leftx.type, rightx.type
            );
        }

        /* special treatment for indexed “=[]” form */
        if (operator === '=[]') {
            var op1 = leftx.operands[1];     /* base pointer/address */

            if (leftx.operator === ':=' && op1[0] === '&') {

                var op2 = makeRValue(rightx);

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

        } else {

            /* pointer‐difference special-case “d” */
            if (operator === '-' && rightx.type === 'p') {
                operator = 'd';
            }

            makeMeta(
                leftx, operator, tp, null,
                makeRValue(leftx),
                makeRValue(rightx)
            );
        }
    }
    parser.binaryOp = binaryOp;


    /* --------------------------------------------------------- *
     *  Multiplication / division with special int-to-float case *
     * --------------------------------------------------------- */
    function mulDivOp(operator, leftx, rightx,
                      sourceCode, sourceOffset) {

        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        var sig = operator + leftx.type + rightx.type;
        var tp  = SUPPORTED_OPS[sig];
        if (tp === undefined) {
            typeError('Invalid types ({$type1} and {$type2})',
                               sourceCode, sourceOffset,
                               leftx.type, rightx.type);
        }

        /* detect (itof X) * 1.0 → itof */
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
                return;
            }
        }

        /* default multiply / divide / mod path */
        makeMeta(
            leftx, operator, tp, null,
            makeRValue(leftx),
            makeRValue(rightx)
        );
    }
    parser.mulDivOp = mulDivOp;


    /* --------------------------------------------------------- *
     *  Assignment helper                                        *
     * --------------------------------------------------------- */
    function assign(x, leftx, rightx,
                    sourceCode, sourceOffset) {

        x      = metaSlot(x);
        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        var lop   = leftx.operator;
        var keep  = 2;          /* operand index to keep for r-value */

        if (leftx.type !== '?' && leftx.type !== rightx.type) {
            typeError(
                'Incompatible types for assignment ({$type1} = {$type2})',
                sourceCode, sourceOffset,
                leftx.type, rightx.type
            );
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
            fail("Invalid lvalue", sourceCode, sourceOffset);
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
    }
    parser.assign = assign;

    /* -----------------------------------------------------------
     *  Unary helpers  (dereference, reference, -, ~, abs/floor,
     *                  int↔float conversions)
     * -------------------------------------------------------- */

    /* *expr  or  [] dereference handling */
    function dereference(operator, expr, sourceCode, sourceOffset) {
        expr = metaSlot(expr);
        if (expr.operator === '+') {
            /*  &a + i   →   PEEK (&a , i)  */
            expr.operator = '=[]';
        } else if (expr.operator === '-' && expr.operands[2] &&
                   expr.operands[2][0] === '#') {
            /*  &a - #n  where n is const → adjust to negative literal */
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
    }
    parser.dereference = dereference;

    /* & (address-of) operator handling */
    function reference(operator, expr, sourceCode, sourceOffset) {

        expr = metaSlot(expr);

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
            expr.operator = '+';                     // &a[i]  →  &a + i

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
            fail("Invalid lvalue", sourceCode, sourceOffset);
        }
    }
    parser.reference = reference;

    /* unary minus (integer/float) */
    function minus(operator, expr/*, src, off*/) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '-', undefined,
            undefined,
            ZEROES[ expr.type ],            // 0  of same type
            makeRValue(expr)
        );
    }
    parser.minus = minus;

    /* bit-wise NOT / logical NOT  (~expr) */
    function not(operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '^', undefined,
            undefined,
            makeRValue(expr),
            '#-1'                                    // XOR with –1
        );
    }
    parser.not = not;

    /* ABS or FLOOR (unary) – operator is already '=abs' or '=floor' */
    function absFloor(operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, operator, undefined,
            undefined,
            makeRValue(expr),
            undefined
        );
    }
    parser.absFloor = absFloor;

    /* int → float */
    function intToFloatConvert(operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '=itof', undefined,
            undefined,
            makeRValue(expr),
            '#1.0'
        );
    }
    parser.intToFloatConvert = intToFloatConvert;

    /* float → int, with constant-fold special-case */
    function floatToIntConvert(operator, expr) {

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
    }
    parser.floatToIntConvert = floatToIntConvert;

    /* -----------------------------------------------------------
     *  UNARY_OPS dispatch table
     * -------------------------------------------------------- */

    function noop() {}

    var UNARY_OPS = {
        '=float':   noop,
        '=funcptr': noop,
        '=int':     noop,
        '=pointer': noop,

        '=*':       dereference,
        '=&':       reference,
        '=-':       minus,
        '=~':       not,
        '=abs':     absFloor,
        '=itof':    intToFloatConvert,
        '=ftoi':    floatToIntConvert,
        '=floor':   absFloor
    };
    parser.UNARY_OPS = UNARY_OPS;

    /* -----------------------------------------------------------
     *  Generic unary operator
     * -------------------------------------------------------- */
    function unaryOp(operator, expr, sourceCode, sourceOffset) {

        expr = metaSlot(expr);

        var key = '=' + operator;                     // e.g. "=abs"

        /* check type support */
        var sig  = key + expr.type;                   // e.g. "=absf"
        var rTyp = SUPPORTED_OPS[ sig ];
        if (rTyp == null) {
            typeError('Invalid type ({$type1})',
                               sourceCode, sourceOffset, expr.type);
        }

        /* dispatch actual work */
        var fn = UNARY_OPS[ key ];
        if (fn) {
            fn(key, expr, sourceCode, sourceOffset);
        }

        /* update resulting type */
        expr.type = rTyp;
    }
    parser.unaryOp = unaryOp;

    /* -----------------------------------------------------------
     *  Symbol declaration helper
     * -------------------------------------------------------- */

    function declare(kind, scope, name, type, readonly, value, sourceCode, sourceOffset) {
        /* emit data / flush pending code */
        if (kind !== undefined) {
            flushMetaCode('');

            if (typeof output === 'function') {       // ‘output’ assumed global
                var line = '';
                if (scope === 'locals') line += (typeof TAB !== 'undefined' ? TAB : '\t');
                if (name != null)       line += name + ':';
                line += '\t' +
                        replace(kind, '?', TYPE_SUFFIXES[type] || '');
                if (value !== undefined) line += ' ' + value;

                output( line );
            }
        }

        /* give any temporary back to the pool */
        returnBack(value);

        /* register in symbol table */
        if (name !== undefined) {
            var table = symbols[scope];
            var prev  = table && table[name];

            if (prev) {
                if (kind !== undefined && prev.kind !== undefined) {
                    fail('Identifier already declared: ' + name,
                                  sourceCode, sourceOffset);
                }
                if (type !== prev.type) {
                    typeError('Type mismatch with previous declaration of ' +
                                       name + ' (was {$type1})',
                                       sourceCode, sourceOffset, prev.type);
                }
                /* inherit old flags */
                kind     = (kind     !== undefined ? kind     : prev.kind);
                readonly = (readonly || prev.readonly);
            }

            /* store / update */
            if (!table) symbols[scope] = table = {};
            table[name] = { type:type, readonly:!!readonly, kind:kind };
        }
    }
    parser.declare = declare;

    /* -----------------------------------------------------------
     *  Flush all queued meta-code into final text output
     * -------------------------------------------------------- */
    function flushMetaCode(prefix) {

        prefix = prefix || '';
        var TABstr = (typeof TAB !== 'undefined') ? TAB : '\t';

        var nextLabel   = TABstr;   // pending label prefix
        var nextComment = '';       // pending trailing comment
        function formatOperand(op) {
            return (op == null ? '' : op);
        }

        var meta = getMetacode();
        for (var i = 0; i < meta.length; ++i) {

            var rec   = meta[i];
            var op    = rec.operator;

            if (op == null) {
                /* empty / removed meta – skip */
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
                continue;
            }

            /* -------------------------------------------------- */
            /* comment pseudo-op (“; …”)                          */
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

                if (nextLabel !== TABstr) {
                    output(prefix + nextLabel + 'NOOP');
                }
                nextLabel = TABstr;

                var gop = META_TO_GAZL[ op.substr(3) ];
                gop      = replace(gop, '?',
                                   TYPE_SUFFIXES[ rec.type ]);

                output(prefix + '\t! ' + gop + ' ' +
                       formatOperand(rec.operands[0]) + ' '   +
                       formatOperand(rec.operands[1]) + ' '   +
                       formatOperand(rec.operands[2]) + nextComment);

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
        clearMetacode();
    }
    parser.flushMetaCode = flushMetaCode;

    /* -----------------------------------------------------------
     *  Identifier lookup helper
     * -------------------------------------------------------- */
    function lookup(x, name, isGlobal, sourceCode, sourceOffset) {

        var sym = symbols;
        var p   = null;

        /* local  ------------------------------------------------*/
        if (!isGlobal && (p = sym.locals['$' + name])) {

            if (p.type === 'A') {
                makeMeta(x, '=&', 'p', undefined,
                                  '$' + name, '*0');
            } else {
                makeMeta(x,
                                  (p.readonly ? ':=' : '='),
                                  p.type,
                                  undefined,
                                  '$' + name,
                                  undefined);
            }
            return;
        }

        /* global -----------------------------------------------*/
        if (isGlobal && (p = sym.globals[name])) {

            if (p.type === 'A') {
                makeMeta(x, ':=', 'p', undefined,
                                  '&' + name, undefined);
            } else {
                makeMeta(x,
                                  (p.readonly ? ':=*' : '=*'),
                                  p.type,
                                  undefined,
                                  '&' + name,
                                  undefined);
            }
            return;
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
            return;
        }

        /* constant / #define -----------------------------------*/
        if ((p = sym.defines[name])) {
            makeMeta(x, ':=', p.type, undefined,
                              '#' + name, undefined);
            return;
        }

        /* not found --------------------------------------------*/
        fail('Undeclared identifier: ' + name,
                      sourceCode, sourceOffset);
    }
    parser.lookup = lookup;

    /* -----------------------------------------------------------
     *  Ensure expression resolves to a compile-time constant
     * -------------------------------------------------------- */
    function makeConstant(x, wantType,
                          sourceCode, sourceOffset) {

        var r = makeRValue(x, '#<&');

        if (x.type !== wantType ||
            span(r[0], '#<&') !== 1) {

            fail(
                bake('Expected constant ' +
                     VERBOSE_TYPES[ wantType ]),
                sourceCode, sourceOffset);
        }
        return r;
    }
    parser.makeConstant = makeConstant;

    /* -----------------------------------------------------------
     *  Constant subtraction helper
     * -------------------------------------------------------- */
    function subConstInt(opL, opR) {

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
    }
    parser.subConstInt = subConstInt;

    /* drop leading “#” helper */
    function dropHash(s) {
        return (s[0] === '#') ? s.substr(1) : s;
    }
    parser.dropHash = dropHash;

    /* printable ASCII table (33–126) */
    var printable = '';
    parser.printable = printable;
    for (var i = 33; i < 127; ++i) {
        printable += char(i);
    }

    /* -----------------------------------------------------------
     *  Dump a string constant into assembly
     * -------------------------------------------------------- */
    function dumpString(label, str) {

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
    }
    parser.dumpString = dumpString;

    /* -----------------------------------------------------------
     *  Manage / share string literals
     * -------------------------------------------------------- */
    function makeString(prefix, x, s,
                        sourceCode, sourceOffset) {

        s += char(0);       // NUL-terminate

        var byteString = '';
        for (var idx = 0; idx < s.length; ++idx) {
            byteString += char(ordinal(s[idx]));
        }
        s = byteString;

        var tbl = ensureStringTable(prefix);

        var entry = tbl.rlookup[s];
        if (entry == null) {

            /* generate unique label */
            var name = '.' + prefix + '_' +
                       (s.replace(/[^0-9a-zA-Z]/g, '')
                          .substr(0, 6)) +
                       ( (randomId + tbl.length)
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
    }
    parser.makeString = makeString;

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

    /* simple “foreach” — fn(element, index) */
    function iterate(arr, fn) {
        for (var i = 0; i < arr.length; ++i) {
            fn(arr[i], i);
        }
    }

    /* -----------------------------------------------------------
     *  Compiler start / end hooks
     * -------------------------------------------------------- */

    function start() {

        /* reset per-compilation state */
        stock = parserState.stock;
        counters = parserState.counters;
        metacode = parserState.metacode;
        strings = parserState.strings;
        switchStack = parserState.switchStack;
        if (!symbols) symbols = {};

        resetStock();
        resetCounters();

        clearMetacode();
        labelCounter    = 0;
        clearSwitchStack();

        /* reset symbol tables */
        symbols.locals   = {};
        symbols.globals  = {};
        symbols.functions = {};
        symbols.defines  = {};

        /* reset deferred string tables */
        resetStrings();

        noForward = false;

        /* random-id seeding */
        if (typeof impalaRandomId !== 'undefined') {
            randomId = impalaRandomId;
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
    }
    parser.start = start;

    function end() {

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
    }
    parser.end = end;
;function root($){return (function(){var _b=_i;return _($)&&(function(){ start(); ; return true})()&&((function(){while((function(){var _b=_i;return FuncDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ExternDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ConstDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GlobalDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _l=_i,_x=(!!_s[_i])&&(++_i,true);_i=_l;return !_x})()&&(function(){ end(); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncDecl($){var $id={},$inp={},$out={},$,$loc={};return (function(){var _b=_i;return FUNCTION($)&&_($)&&Identifier($id)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ assert(validateStock('%')); assert(validateStock('<')); output(''); output(';-----------------------------------------------------------------------------'); /* declare the function symbol */ declare( 'FUNC',           // kind
                                                                             'functions',      // scope
                                                                             $id._,              // name
                                                                             'U',              // type
                                                                             true,             // read-only
                                                                             undefined,        // no init
                                                                             _s, _i ); ; return true})()&&ArgsDecl($inp)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){var _b=_i;return RETURNS($)&&_($)&&VarDecl($out)&&(function(){ declare( 'OUT?', 'locals', '$' + $out.name, $out.type, false, ($out.size !== undefined ? '*' + $out.size : undefined), _s, _i ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ /* implicit 1-word return */ declare( 'PARA', 'locals', undefined, '?', false, '*1', _s, _i ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ /* declare input parameters */ iterate($inp._, function (p) { declare( 'INP?', 'locals', '$' + p.name, p.type, true, (p.size !== undefined ? '*' + p.size : undefined), _s, _i ); }); ; return true})()&&((function(){var _b=_i;return LOCALS($)&&_($)&&LocalsDecl($loc)&&(function(){ iterate($loc._, function (v) { declare( 'LOC?', 'locals', '$' + v.name, v.type, false, (v.size !== undefined ? '*' + v.size : undefined), _s, _i ); }); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ output(';-----------------------------------------------------------------------------'); ; return true})()&&Block($)&&(function(){ /* wrap-up body */ processBranches(); emit('--^', undefined, undefined, undefined, undefined); flushMetaCode('\t'); prune(symbols.locals); labelCounter = 0; output(''); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ExternDecl($){var $id={};return (function(){var _b=_i;return EXTERN($)&&_($)&&(function(){ $.scope = 'globals'; ; return true})()&&(function(){var _b=_i;return (function(){var _b=_i;return FUNCTION($)&&(function(){ $.type  = 'U';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NATIVE($)&&(function(){ $.type  = 'N';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||ARRAY($)&&(function(){ $.type  = 'A'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&Identifier($id)&&(function(){ $.name  = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ declare( undefined,                 // no section for extern
                                                                             $.scope, $.name, $.type, false,                     // not readonly
                                                                             '?', _s, _i ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ConstDecl($){var $type={},$t,$nf,$id={},$x={};return (function(){var _b=_i;return CONST($)&&_($)&&BASE_TYPE($type)&&_($)&&(function(){ $t  = CASTS_TO_TYPES[$type._]; $nf = noForward; noForward = true; ; return true})()&&Identifier($id)&&(function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($x)&&(function(){ declare( '! DEF?', 'defines', $id._, $t, true, makeConstant($x._, $t, _s, _i), _s, _i ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ declare( undefined, 'defines', $id._, $t, true, undefined, _s, _i ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ noForward = $nf; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GlobalDecl($){var $section,$v={},$init,$x={},$a={},$d={};return (function(){var _b=_i;return (function(){var _b=_i;return GLOBAL($)&&(function(){ $section = 'GLOB'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||READONLY($)&&(function(){ $section = 'CNST'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||TEMPORARY($)&&(function(){ $section = 'TEMP'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&(function(){var _b=_i;return VarDecl($v)&&(function(){ declare( $section, 'globals', undefined, $v.type, ($section === 'CNST'), '*1', _s, _i ); $init = ZEROES[$v.type]; ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($x)&&(function(){ $init = makeConstant($x._, $v.type, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ declare( 'DAT?', 'globals', $v.name, $v.type, ($section === 'CNST'), $init, _s, _i ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($a)&&(function(){ declare( $section, 'globals', $a.name, 'A', ($section === 'CNST'), '*' + $a.size, _s, _i ); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&InitList($d)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function InitList($){var $d,$type,$x={};return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ $d = ' '; $type = undefined; ; return true})()&&((function(){var _b=_i;return Expr($x)&&(function(){ var xMeta = metaSlot($x._); $type = xMeta.type; $d += makeConstant(xMeta, $type, _s, _i); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&Expr($x)&&(function(){ var xMeta = metaSlot($x._); var xType = xMeta.type; var constant = makeConstant(xMeta, xType, _s, _i); /* decide if we need to flush DATA */ if (  constant[0] === '<' || $d[1] === '<' || ($d + ' ' + constant).length >= 55) { declare( 'DATA', 'globals', undefined, xType, true, $d.substr(1), _s, _i ); $d = ''; } $d += ' ' + constant; $type = xType; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ if ($d.substr(1) !== '') { declare( 'DATA', 'globals', undefined, $type, true, $d.substr(1), _s, _i ); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArgsDecl($){var $v={};return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return VarDecl($v)&&(function(){ var entry = {}; entry.type = $v.type; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($v)&&(function(){ var entry = {}; entry.type = $v.type; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LocalsDecl($){var $v={};return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return (function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ var entry = {}; entry.type = $v.type; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ var entry = {}; entry.type = $v.type; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function VarDecl($){var $type={},$id={};return (function(){var _b=_i;return BASE_TYPE($type)&&_($)&&Identifier($id)&&(function(){ $.type = CASTS_TO_TYPES[$type._]; $.name = $id._; $.size = undefined; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArrayDecl($){var $id={},$x={},$size;return (function(){var _b=_i;return ARRAY($)&&_($)&&Identifier($id)&&(_s[_i]==="[")&&(++_i,true)&&_($)&&Expr($x)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ $size = makeConstant($x._, 'i', _s, _i); $.type = 'A'; $.name = $id._; $.size = dropHash($size); returnBack($size); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Statement($){var $label={};return (function(){var _b=_i;return (function(){ var snippet = _s.substr(_i); var cut     = find(snippet, "{;\r\n"); var txt     = (cut >= 0 ? snippet.substr(0, cut) : snippet); emitMeta({ operator:';', type:undefined, operands:[ txt, undefined, undefined ] }); ; return true})()&&((function(){while((function(){var _b=_i;return Identifier($label)&&(_s[_i]===":")&&(++_i,true)&&_($)&&(function(){ emitMeta({ operator:'<--', type:undefined, operands:[ '@' + $label._, undefined, undefined ] }); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Assert($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Block($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Copy($)||(_im=(_i>_im?_i:_im),_i=_b,false)||DoWhile($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Loop($)||(_im=(_i>_im?_i:_im),_i=_b,false)||For($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Goto($)||(_im=(_i>_im?_i:_im),_i=_b,false)||If($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Switch($)||(_im=(_i>_im?_i:_im),_i=_b,false)||While($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ releaseMeta($); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Expr($){var $r={};return (function(){var _b=_i;return Bitwise($)&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($r)&&(function(){ if (!dry) assign($, $, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Bitwise($){var $op={},$r={};return (function(){var _b=_i;return AddSub($)&&((function(){while((function(){var _b=_i;return BITWISE_OP($op)&&_($)&&AddSub($r)&&(function(){ if (!dry) binaryOp($op._, $, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function AddSub($){var $op={},$r={};return (function(){var _b=_i;return MulDiv($)&&((function(){while((function(){var _b=_i;return ADDSUB_OP($op)&&_($)&&MulDiv($r)&&(function(){ if (!dry) binaryOp($op._, $, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function MulDiv($){var $op={},$r={};return (function(){var _b=_i;return PrePost($)&&((function(){while((function(){var _b=_i;return MULDIV_OP($op)&&_($)&&PrePost($r)&&(function(){ if (!dry) mulDivOp($op._, $, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function PrePost($){var $op={};return (function(){var _b=_i;return (function(){var _b=_i;return PREFIX_OP($op)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="(")&&(++_i,true)&&_($)&&BASE_TYPE($op)&&_($)&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&PrePost($)&&(function(){ if (!dry) unaryOp($op._, $, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Value($)&&((function(){while((function(){var _b=_i;return FuncCall($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Subscript($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Subscript($){var $s={};return (function(){var _b=_i;return (_s[_i]==="[")&&(++_i,true)&&_($)&&Expr($s)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ if (!dry) binaryOp('=[]', $, $s._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncCall($){var $type,$;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ if (!dry) { $.count = 0; $.base  = borrowForCall(); } ; return true})()&&((function(){var _b=_i;return Argument($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&Argument($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if (!dry) { var callee = metaSlot($); if (span(callee.type, 'FN') !== 1) { typeError( 'Invalid type for function call ({$type1})', _s, _i, callee.type ); } var func = makeRValue(callee, '&^$%'); emit('()', '?', func, '%' + $.base, '*' + ($.count + 1)); returnBack(func); while ($.count-- > 0) { returnBack('%' + ($.base + $.count + 1)); } makeMeta(callee, ':=', '?', undefined, '%' + $.base, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Argument($){var $a={};return (function(){var _b=_i;return Expr($a)&&(function(){ if (!dry) { ++$.count; makeArgValue($a._, $.base + $.count); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Group($){return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BoolGroup($){var $label;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ $label = undefined; ; return true})()&&And($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="||")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('t'); } emit('?->', true, $label, undefined, undefined); ; return true})()&&And($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if ($label !== undefined) { emit('<-?', true, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function And($){var $label;return (function(){var _b=_i;return (function(){ $label = undefined; ; return true})()&&Comp($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="&&")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('f'); } emit('?->', false, $label, undefined, undefined); ; return true})()&&Comp($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ if ($label !== undefined) { emit('<-?', false, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Comp($){var $op={},$r={};return (function(){var _b=_i;return (_s[_i]==="!")&&(++_i,true)&&_($)&&Comp($)&&(function(){ emit('!', undefined, undefined, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = true; ; return true})()&&(function(){var _l=_i,_x=Group($);_i=_l;return !_x})()&&(function(){ dry = false; ; return true})()&&BoolGroup($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = false; ; return true})()&&Expr($)&&COMP_OP($op)&&_($)&&Expr($r)&&(function(){ binaryOp($op._, $._, $r._, _s, _i); emitMeta($._); releaseMeta($); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Assert($){var $okLabel,$start,$exprText,$str={},$;return (function(){var _b=_i;return ASSERT($)&&_($)&&(function(){ $okLabel = newLabel('a'); emit('<> ==', 'i', '#DEBUG', '#0', $okLabel); $start    = _i; $exprText = ''; ; return true})()&&BoolGroup($str)&&(function(){ $exprText = _s.substring($start, _i); $exprText = $exprText.replace(/[ \t\r\n]+$/, ''); ; return true})()&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ emit('?->', true, $okLabel, undefined, undefined); var r = borrowForCall(); /* store the assert-string constant */ makeString('a', $str._, $exprText, _s, _i); /* argument goes into %<r+1> */ makeArgValue($str._, r + 1); /* call the failure handler */ emit('()', '?', '^assertFail', '%' + r, '*1'); /* tidy temporaries */ returnBack('%' + (r + 1)); returnBack('%' +  r); /* continue after OK-label */ emit('<-?', true, $okLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Block($){return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while(Statement($));})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Goto($){var $label={};return (function(){var _b=_i;return GOTO($)&&_($)&&Identifier($label)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ emit('-->', undefined, '@' + $label._, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function If($){var $dontLabel,$doneLabel;return (function(){var _b=_i;return IF($)&&_($)&&BoolGroup($)&&(function(){ $dontLabel = newLabel('f'); emit('?->', false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){var _b=_i;return ELSE($)&&_($)&&(function(){ $doneLabel = newLabel('e'); emit('-->', undefined, $doneLabel, undefined, undefined); emit('<-?',  false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('<--', undefined, $doneLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ emit('<-?', false, $dontLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DoWhile($){var $loopLabel;return (function(){var _b=_i;return DO($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<-?', false, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&WHILE($)&&_($)&&BoolGroup($)&&(function(){ emit('?->', true, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Loop($){var $loopLabel;return (function(){var _b=_i;return LOOP($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function For($){var $var={},$gotInit,$init={},$toExpr={},$type,$to,$noLoopLabel,$loopLabel,$body={};return (function(){var _b=_i;return FOR($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Variable($var)&&(function(){ /* loop-variable must be local, modifiable int / pointer */ var varMeta = metaSlot($var._); if (varMeta.operator !== '=' || span(varMeta.type, "ip") === 0) { fail( 'For variable must be a local modifiable int or pointer variable', _s, _i ); } $gotInit = false;            /* flag to detect an explicit start value */ ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($init)&&(function(){ assign($init._, $var._, $init._, _s, _i); $gotInit = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&TO($)&&_($)&&Expr($toExpr)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var varMeta  = metaSlot($var._); var toMeta   = metaSlot($toExpr._); if (toMeta.type !== varMeta.type) { typeError( 'Incompatible types ({$type1} and {$type2})', _s, _i, varMeta.type, toMeta.type ); } /* constant upper bound */ $to = makeRValue(toMeta); /* initial comparison  (var < to)                         */ emit( '<', toMeta.type, undefined, $gotInit ? metaSlot($init._).operands[1]     /* start value from “var = expr” */ : varMeta.operands[1],            /* or the original variable */ $to ); if ($gotInit) { releaseMeta($init._); } /* branch-out   and  loop label */ $noLoopLabel = newLabel('e'); emit('?->', false, $noLoopLabel, undefined, undefined); $loopLabel   = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($body)&&(function(){ var varMeta = metaSlot($var._); /* increment + jump back */ emit( '...', varMeta.type, varMeta.operands[1],        /* address of loop variable */ $to, $loopLabel ); emit('<-?', false, $noLoopLabel, undefined, undefined); returnBack($to); releaseMeta(varMeta); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Copy($){var $l={},$f={},$t={},$length,$type,$;return (function(){var _b=_i;return COPY($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($l)&&FROM($)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var fromMeta = metaSlot($f._); var toMeta   = metaSlot($t._); $length = makeConstant($l._, 'i', _s, _i); var lengthHash = dropHash($length); if (fromMeta.type + toMeta.type !== 'pp') { returnBack($length); typeError( 'Invalid types ({$type1} and {$type2})', _s, _i, fromMeta.type, toMeta.type ); } var copyMeta = metaSlot($l._); makeMeta( copyMeta, 'copy', '?', makeRValue(toMeta, '&$%'), makeRValue(fromMeta, '&$%'), '*' + lengthHash ); emitMeta(copyMeta); returnBack($length); releaseMeta(copyMeta); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Switch($){var $f={},$t={},$size,$switcher,$,$switchExit,$progress,$stmt={};return (function(){var _b=_i;return SWITCH($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s.substr(_i,2)==="==")&&(_i+=2,true)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(function(){ var switchMeta = metaSlot($); /* the switch expression must be an int */ if (switchMeta.type !== 'i') { fail('Switch expression needs to be int', _s, _i); } /* lower bound (compile-time constant) */ switchMeta.from = makeConstant($f._, 'i', _s, _i); /*    size = to - from   */ $size = subConstInt( makeConstant($t._, 'i', _s, _i), switchMeta.from ); /*   switcher = (expr − from)   */ $switcher = subConstInt( makeRValue(switchMeta, '$%'), switchMeta.from ); switchMeta.switchLabel = newLabel('s'); $switchExit              = newLabel('e'); pushSwitchContext(switchMeta); emit( '-->#', switchMeta.type, $switcher, '*' + dropHash($size), switchMeta.switchLabel ); returnBack($switcher); returnBack($size); $progress = undefined;       /* track case / default presence */ ; return true})()&&(_s[_i]===")")&&(++_i,true)&&_($)&&(_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return CASE($)&&_($)&&(function(){ /* multiple CASE groups → fall-through handled here */ if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } else { $progress = 'gotCases'; } /* dump the literal “case …” comment */ var snippet = _s.substr(_i); var pos     = find(snippet, ":\r\n"); if (pos >= 0) { snippet = snippet.substr(0, pos); } emit( ';', undefined, 'case ' + snippet, undefined, undefined ); ; return true})()&&CaseExpr($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&CaseExpr($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DEFAULT($)&&_($)&&(function(){ if ($progress === 'gotDefault') { fail('Default case already defined'); } else if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } var ctx = peekSwitchContext(); emit(';',    undefined, 'default',       undefined, undefined); emit('<--',  undefined, ctx.switchLabel,  undefined, undefined); $progress = 'gotDefault'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(_s[_i]===":")&&(++_i,true)&&_($)&&Statement($stmt)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ var ctx = popSwitchContext() || metaSlot($); /* no explicit “default” → hook it up now                        */ if ($progress !== 'gotDefault') { emit('<--', undefined, ctx.switchLabel, undefined, undefined); } emit('<--', undefined, $switchExit, undefined, undefined); returnBack(ctx.from); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CaseExpr($){var $n;return (function(){var _b=_i;return Expr($)&&(function(){ /* offset = constant(expr) – switch.from                         */ var ctx      = peekSwitchContext(); var caseMeta = metaSlot($); var baseFrom = (ctx ? ctx.from : caseMeta.from); var baseLabel = (ctx ? ctx.switchLabel : caseMeta.switchLabel); $n = subConstInt( makeConstant(caseMeta, 'i', _s, _i), baseFrom ); /* create label for this case                                     */ emit( '<--', undefined, baseLabel + '#' + dropHash($n), undefined, undefined ); returnBack($n); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function While($){var $loopLabel,$exitLabel;return (function(){var _b=_i;return WHILE($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&BoolGroup($)&&(function(){ $exitLabel = newLabel('e'); emit('?->', false, $exitLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); emit('<-?', false, $exitLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Value($){var $f={},$i={},$s={};return (function(){var _b=_i;return Group($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FloatLiteral($f)&&(function(){ if (!dry) { makeMeta($, ':=', 'f', undefined, '#' + $f._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||IntegerLiteral($i)&&(function(){ if (!dry) { makeMeta($, ':=', 'i', undefined, '#' + $i._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||StringLiteral($s)&&(function(){ if (!dry) { makeString('s', $, evaluate($s._), _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULL($)&&_($)&&(function(){ if (!dry) { makeMeta($, ':=', 'p', undefined, '&NULL', undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULLFUNC($)&&_($)&&(function(){ if (!dry) { makeMeta($, ':=', 'F', undefined, '&NULL', undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Variable($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Variable($){var $global,$id={};return (function(){var _b=_i;return (function(){ $global = false; ; return true})()&&((function(){var _b=_i;return GLOBAL($)&&_($)&&(function(){ $global = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&Identifier($id)&&(function(){ if (!dry) { lookup($, $id._, $global, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Identifier($){return (function(){var _b=_i;return (function(){var _l=_i,_x=KEYWORD($);_i=_l;return !_x})()&&(function(){var _m=_i;return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$".indexOf(_s[_i])>=0)&&(++_i,true)&&((function(){while(SYMBOL_CHAR($));})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FloatLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&(_s[_i]===".")&&(++_i,true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&((function(){var _b=_i;return (function(){var _b=_i;return (_s[_i]==="E")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="e")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IntegerLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&(function(){var _b=_i;return (_s.substr(_i,2)==="0x")&&(_i+=2,true)&&((function(){for(var _n=0;HEX($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="'")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(_s[_i]==="'")&&(++_i,true);_i=_l;return !_x})()&&ASCII($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function StringLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="\"")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\"\\\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="\\")&&(++_i,true)&&(function(){var _b=_i;return (!!_s[_i]&&"\"\\bfnrt".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="u")&&(++_i,true)&&HEX($)&&HEX($)&&HEX($)&&HEX($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="\"")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function KEYWORD($){return (function(){var _b=_i;return ABS($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ARRAY($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ASSERT($)||(_im=(_i>_im?_i:_im),_i=_b,false)||CASE($)||(_im=(_i>_im?_i:_im),_i=_b,false)||CONST($)||(_im=(_i>_im?_i:_im),_i=_b,false)||COPY($)||(_im=(_i>_im?_i:_im),_i=_b,false)||DEFAULT($)||(_im=(_i>_im?_i:_im),_i=_b,false)||DO($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ELSE($)||(_im=(_i>_im?_i:_im),_i=_b,false)||EXTERN($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FLOAT($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FLOOR($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FOR($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FROM($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FTOI($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FUNCPTR($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FUNCTION($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GLOBAL($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GOTO($)||(_im=(_i>_im?_i:_im),_i=_b,false)||IF($)||(_im=(_i>_im?_i:_im),_i=_b,false)||INT($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ITOF($)||(_im=(_i>_im?_i:_im),_i=_b,false)||LOCALS($)||(_im=(_i>_im?_i:_im),_i=_b,false)||LOOP($)||(_im=(_i>_im?_i:_im),_i=_b,false)||NATIVE($)||(_im=(_i>_im?_i:_im),_i=_b,false)||NULL($)||(_im=(_i>_im?_i:_im),_i=_b,false)||NULLFUNC($)||(_im=(_i>_im?_i:_im),_i=_b,false)||POINTER($)||(_im=(_i>_im?_i:_im),_i=_b,false)||READONLY($)||(_im=(_i>_im?_i:_im),_i=_b,false)||RETURNS($)||(_im=(_i>_im?_i:_im),_i=_b,false)||SWITCH($)||(_im=(_i>_im?_i:_im),_i=_b,false)||TEMPORARY($)||(_im=(_i>_im?_i:_im),_i=_b,false)||TO($)||(_im=(_i>_im?_i:_im),_i=_b,false)||WHILE($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ABS($){return (function(){var _b=_i;return (_s.substr(_i,3)==="abs")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ARRAY($){return (function(){var _b=_i;return (_s.substr(_i,5)==="array")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ASSERT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="assert")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CASE($){return (function(){var _b=_i;return (_s.substr(_i,4)==="case")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CONST($){return (function(){var _b=_i;return (_s.substr(_i,5)==="const")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COPY($){return (function(){var _b=_i;return (_s.substr(_i,4)==="copy")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DEFAULT($){return (function(){var _b=_i;return (_s.substr(_i,7)==="default")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DO($){return (function(){var _b=_i;return (_s.substr(_i,2)==="do")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ELSE($){return (function(){var _b=_i;return (_s.substr(_i,4)==="else")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function EXTERN($){return (function(){var _b=_i;return (_s.substr(_i,6)==="extern")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FLOAT($){return (function(){var _b=_i;return (_s.substr(_i,5)==="float")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FLOOR($){return (function(){var _b=_i;return (_s.substr(_i,5)==="floor")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FOR($){return (function(){var _b=_i;return (_s.substr(_i,3)==="for")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FROM($){return (function(){var _b=_i;return (_s.substr(_i,4)==="from")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FTOI($){return (function(){var _b=_i;return (_s.substr(_i,4)==="ftoi")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FUNCPTR($){return (function(){var _b=_i;return (_s.substr(_i,7)==="funcptr")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
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
function RETURNS($){return (function(){var _b=_i;return (_s.substr(_i,7)==="returns")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SWITCH($){return (function(){var _b=_i;return (_s.substr(_i,6)==="switch")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TEMPORARY($){return (function(){var _b=_i;return (_s.substr(_i,9)==="temporary")&&(_i+=9,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TO($){return (function(){var _b=_i;return (_s.substr(_i,2)==="to")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function WHILE($){return (function(){var _b=_i;return (_s.substr(_i,5)==="while")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BITWISE_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,2)==="<<")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,3)===">>>")&&(_i+=3,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)===">>")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="|")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="|")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="&")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="&")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="^")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ADDSUB_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="+")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="-")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function MULDIV_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="*")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="/")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="/")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="%")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COMP_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,2)==="<=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)===">=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="!=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="==")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="<")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===">")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BASE_TYPE($){var $;return (function(){var _b=_i;return INT($)&&(function(){ $._ = 'int'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||FLOAT($)&&(function(){ $._ = 'float'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||FUNCPTR($)&&(function(){ $._ = 'funcptr'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||POINTER($)&&(function(){ $._ = 'pointer'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BUILT_IN($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,3)==="abs")&&(_i+=3,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,5)==="floor")&&(_i+=5,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,4)==="itof")&&(_i+=4,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,4)==="ftoi")&&(_i+=4,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function PREFIX_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="-")&&(++_i,true)&&(function(){var _l=_i,_x=(function(){var _b=_i;return (_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DIGIT($)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="~")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="&")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="*")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||BUILT_IN($)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SYMBOL_CHAR($){return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function HEX($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789ABCDEFabcdef".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DIGIT($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ASCII($){return (function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function _($){return (function(){var _b=_i;return ((function(){while((function(){var _b=_i;return ((function(){for(var _n=0;(!!_s[_i]&&" \t\r\n".indexOf(_s[_i])>=0)&&(++_i,true);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)||COMMENT($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COMMENT($){return (function(){var _b=_i;return (_s.substr(_i,2)==="/*")&&(_i+=2,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(_s.substr(_i,2)==="*/")&&(_i+=2,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s.substr(_i,2)==="*/")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="//")&&(_i+=2,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\r\n".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
var _i=0,_im=0,_o={_:void 0},_b=root(_o);
return [_b,_o._,(_b?_i:_im)];
});
if (typeof module !== 'undefined' && module.exports) {
	module.exports = impalaCompiler;
	module.exports.impalaCompiler = impalaCompiler;
	module.exports.default = impalaCompiler;
}
