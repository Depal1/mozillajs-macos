var n = 0;
for (var i = 0; i < 500000; i++) {
    var o = { a: i, b: "s" + i, c: [i, i + 1] };
    n += o.a;
}
print(n);
