var count = 0;

function bar(f) {
    if (++count < 10)
        return;
    count = 0;
    throw f;
}

noInline(bar);

function foo(a) {
    var x = a + 1;
    for (;;) {
        bar(function() { return x; });
    }
}

noInline(foo);

for (var i = 0; i < testLoopCount; ++i) {
    try {
        foo(i);
    } catch (f) {
        var result = f();
        if (result != i + 1)
            throw "Error: bad result for i = " + i + ": " + result;
    }
}

