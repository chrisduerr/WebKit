function foo(o, value) {
    o.f = value;
    return o.f;
}

noInline(foo);

var counter = 0;

function test(o, value, expectedCount) {
    var result = foo(o, value);
    if (result != value)
        throw new Error("Bad result: " + result);
    if (counter != expectedCount)
        throw new Error("Bad counter value: " + counter);
}

for (var i = 0; i < testLoopCount; ++i) {
    var o = {
        get f() {
            return this._f;
        },
        set f(value) {
            counter++;
            this._f = value;
        }
    };
    test(o, i, counter + 1);
}
